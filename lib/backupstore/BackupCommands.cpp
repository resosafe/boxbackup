// --------------------------------------------------------------------------
//
// File
//		Name:    BackupCommands.cpp
//		Purpose: Implement commands for the Backup store protocol
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <set>
#include <sstream>

#include "autogen_BackupProtocol.h"
#include "autogen_RaidFileException.h"
#include "BackupConstants.h"
#include "BackupStoreContext.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BackupsList.h"
#include "BackupStoreFile.h"
#include "BackupStoreInfo.h"
#include "BufferedStream.h"
#include "CollectInBufferStream.h"
#include "FileStream.h"
#include "InvisibleTempFileStream.h"
#include "RaidFileController.h"
#include "StreamableMemBlock.h"

#include "MemLeakFindOn.h"

#define PROTOCOL_ERROR(code) \
	std::auto_ptr<BackupProtocolMessage>(new BackupProtocolError( \
		BackupProtocolError::ErrorType, \
		BackupProtocolError::code));

#define CHECK_PHASE(phase) \
	if(rContext.GetPhase() != BackupStoreContext::phase) \
	{ \
		BOX_ERROR("Received command " << ToString() << " " \
			"in wrong protocol phase " << rContext.GetPhaseName() << ", " \
			"expected in " #phase); \
		return PROTOCOL_ERROR(Err_NotInRightProtocolPhase); \
	}

#define CHECK_WRITEABLE_SESSION \
	if(rContext.SessionIsReadOnly()) \
	{ \
		BOX_ERROR("Received command " << ToString() << " " \
			"in a read-only session"); \
		return PROTOCOL_ERROR(Err_SessionReadOnly); \
	}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolMessage::HandleException(BoxException& e)
//		Purpose: Return an error message appropriate to the passed-in
//		exception, or rethrow it.
//		Created: 2014/09/14
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolReplyable::HandleException(BoxException& e) const
{
	if(e.GetType() == RaidFileException::ExceptionType &&
		e.GetSubType() == RaidFileException::RaidFileDoesntExist)
	{
		return PROTOCOL_ERROR(Err_DoesNotExist);
	}
	else if (e.GetType() == BackupStoreException::ExceptionType)
	{
		if(e.GetSubType() == BackupStoreException::AddedFileDoesNotVerify)
		{
			return PROTOCOL_ERROR(Err_FileDoesNotVerify);
		}
		else if(e.GetSubType() == BackupStoreException::AddedFileExceedsStorageLimit)
		{
			return PROTOCOL_ERROR(Err_StorageLimitExceeded);
		}
		else if(e.GetSubType() == BackupStoreException::MultiplyReferencedObject)
		{
			return PROTOCOL_ERROR(Err_MultiplyReferencedObject);
		}
		else if(e.GetSubType() == BackupStoreException::CouldNotFindEntryInDirectory)
		{
			return PROTOCOL_ERROR(Err_DoesNotExistInDirectory);
		}
		else if(e.GetSubType() == BackupStoreException::NameAlreadyExistsInDirectory)
		{
			return PROTOCOL_ERROR(Err_TargetNameExists);
		}
		else if(e.GetSubType() == BackupStoreException::ObjectDoesNotExist)
		{
			return PROTOCOL_ERROR(Err_DoesNotExist);
		}
		else if(e.GetSubType() == BackupStoreException::PatchChainInfoBadInDirectory)
		{
			return PROTOCOL_ERROR(Err_PatchConsistencyError);
		}
		else if(e.GetSubType() == BackupStoreException::CannotResumeUpload)
		{
			return PROTOCOL_ERROR(Err_CannotResumeUpload);
		}
	}

	throw;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolVersion::DoCommand(Protocol &,
//			 BackupStoreContext &)
//		Purpose: Return the current version, or an error if the
//			 requested version isn't allowed
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolVersion::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Version)

	// Correct version?
	if(mVersion != BACKUP_STORE_SERVER_VERSION)
	{
		return PROTOCOL_ERROR(Err_WrongVersion);
	}

	// Mark the next phase
	rContext.SetPhase(BackupStoreContext::Phase_Login);

	// Return our version
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolVersion(BACKUP_STORE_SERVER_VERSION));
}


std::auto_ptr<BackupProtocolMessage> DoLogin(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext, int32_t ClientID, int32_t Flags, int32_t Version)
{

	// Set the protocol version, this can be useful
	// in order to know what the client supports
	rContext.SetProtocolVersion(Version);
	
	// Check given client ID against the ID in the certificate certificate
	// and that the client actually has an account on this machine
	if(ClientID != rContext.GetClientID())
	{
		BOX_WARNING("Failed login from client ID " <<
			BOX_FORMAT_ACCOUNT(ClientID) << ": "
			"wrong certificate for this account");
		return PROTOCOL_ERROR(Err_BadLogin);
	}

	if(!rContext.GetClientHasAccount())
	{
		BOX_WARNING("Failed login from client ID " <<
			BOX_FORMAT_ACCOUNT(ClientID) << ": "
			"no such account on this server");
		return PROTOCOL_ERROR(Err_BadLogin);
	}

	// If we need to write, check that nothing else has got a write lock
	if((Flags & BackupProtocolLogin::Flags_ReadOnly) != BackupProtocolLogin::Flags_ReadOnly)
	{
		// See if the context will get the lock
		if(!rContext.AttemptToGetWriteLock())
		{
			BOX_WARNING("Failed to get write lock for Client ID " <<
				BOX_FORMAT_ACCOUNT(ClientID));
			return PROTOCOL_ERROR(Err_CannotLockStoreForWriting);
		}

		// Debug: check we got the lock
		ASSERT(!rContext.SessionIsReadOnly());
	}

	// Load the store info
	rContext.LoadStoreInfo();

	if(!rContext.GetBackupStoreInfo().IsAccountEnabled())
	{
		BOX_WARNING("Refused login from disabled client ID " <<
			BOX_FORMAT_ACCOUNT(ClientID));
		return PROTOCOL_ERROR(Err_DisabledAccount);
	}

	// Get the last client store marker
	int64_t clientStoreMarker = rContext.GetClientStoreMarker();

	// Mark the next phase
	rContext.SetPhase(BackupStoreContext::Phase_Commands);

	// Log login
	BOX_NOTICE("Login from Client ID " <<
		BOX_FORMAT_ACCOUNT(ClientID) << " "
		"(name=" << rContext.GetAccountName() << "): " <<
		(rContext.SessionIsReadOnly()?"Read/Write":"Read-only") << 
		" (protocol version: " << Version << ")" <<
		" from " << rContext.GetConnectionDetails());

	// Get the usage info for reporting to the client
	int64_t blocksUsed = 0, blocksSoftLimit = 0, blocksHardLimit = 0;
	rContext.GetStoreDiscUsageInfo(blocksUsed, blocksSoftLimit, blocksHardLimit);

	// Return success
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolLoginConfirmed(clientStoreMarker, blocksUsed, blocksSoftLimit, blocksHardLimit));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolLogin::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Return the current version, or an error if the requested version isn't allowed
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolLogin::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Login)

		return DoLogin(rProtocol, rContext, mClientID, mFlags, mVersion);

}


// // --------------------------------------------------------------------------
// //
// // Function
// //		Name:    BackupProtocolLogin2::DoCommand(Protocol &, BackupStoreContext &)
// //		Purpose: Return the current version, or an error if the requested version isn't allowed
// //		Created: 2023/10/27
// //
// // --------------------------------------------------------------------------
// std::auto_ptr<BackupProtocolMessage> BackupProtocolLogin2::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
// {
// 	CHECK_PHASE(Phase_Login)

// 	return DoLogin(rProtocol, rContext, mClientID, mFlags, mVersion);
// }


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolFinished::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Marks end of conversation (Protocol framework handles this)
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolFinished::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	// can be called in any phase

	
	SessionInfos& stats = rContext.GetSessionInfos();

	BOX_NOTICE("Session finished for Client ID " <<
		BOX_FORMAT_ACCOUNT(rContext.GetClientID()) << " "
		"(name=" << rContext.GetAccountName() << "), "
		"infos " << rContext.GetConnectionDetails() << ","
		"added files : "<<stats.GetAddedFilesCount()  << " (" << stats.GetAddedFilesBlocksCount() << " blocks), " 
		"deleted files : "<<stats.GetDeletedFilesCount()  << " (" << stats.GetDeletedFilesBlocksCount() << " blocks), "
		"added dirs : "<<stats.GetAddedDirectoriesCount()  << ", " 
		"deleted dirs : "<<stats.GetDeletedDirectoriesCount()  << ", "
		"time :" << stats.ElapsedTime() << "s"

		);

	// Let the context know about it
	rContext.ReceivedFinishCommand();

	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolFinished);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolListDirectory::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Command to list a directory
//		Created: 2003/09/02
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolListDirectory::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Store the listing to a stream
	std::auto_ptr<CollectInBufferStream> stream(new CollectInBufferStream);

	// Ask the context for a directory
	const BackupStoreDirectory &rdir(
		rContext.GetDirectory(mObjectID));
	rdir.WriteToStream(*stream, mFlagsMustBeSet,
		mFlagsNotToBeSet, 
		mSnapshotTime,
		mSendAttributes,
		false /* never send dependency info to the client */,
		rContext.GetProtocolVersion());

	stream->SetForReading();

	// Get the protocol to send the stream
	rProtocol.SendStreamAfterCommand(static_cast< std::auto_ptr<IOStream> > (stream));

	return std::auto_ptr<BackupProtocolMessage>(
		new BackupProtocolSuccess(mObjectID));
}




// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolStoreFile::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Command to store a file on the server
//		Created: 2003/09/02
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolStoreFile::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext,
	IOStream& rDataStream) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	std::auto_ptr<BackupProtocolMessage> hookResult =
		rContext.StartCommandHook(*this);
	if(hookResult.get())
	{
		return hookResult;
	}

	// Check that the diff from file actually exists, if it's specified
	if(mDiffFromFileID != 0)
	{
		if(!rContext.ObjectExists(mDiffFromFileID,
			BackupStoreContext::ObjectExists_File))
		{
			return PROTOCOL_ERROR(Err_DiffFromFileDoesNotExist);
		}
	}

	// Ask the context to store it
	int64_t id = rContext.AddFile(rDataStream, mDirectoryObjectID,
		mModificationTime, mAttributesHash, mDiffFromFileID,
		mFilename,
		true /* mark files with same name as old versions */,
		0 /* don't support resuming */);

	// Tell the caller what the file ID was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(id));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolStoreFileWithResume::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Command to store a file on the server with an optional resumable status
//		Created: 2023/09/11
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolStoreFileWithResume::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext,
	IOStream& rDataStream) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	std::auto_ptr<BackupProtocolMessage> hookResult =
		rContext.StartCommandHook(*this);
	if(hookResult.get())
	{
		return hookResult;
	}

	// Check that the diff from file actually exists, if it's specified
	if(mDiffFromFileID != 0)
	{
		if(!rContext.ObjectExists(mDiffFromFileID,
			BackupStoreContext::ObjectExists_File))
		{
			return PROTOCOL_ERROR(Err_DiffFromFileDoesNotExist);
		}
	}

	// Ask the context to store it
	int64_t id = rContext.AddFile(rDataStream, mDirectoryObjectID,
		mModificationTime, 
		mAttributesHash, mDiffFromFileID,
		mFilename,
		true /* mark files with same name as old versions */,
		mResumeOffset);

	// Tell the caller what the file ID was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(id));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetObject::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Command to get an arbitary object from the server
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetObject::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Check the object exists
	if(!rContext.ObjectExists(mObjectID))
	{
		return PROTOCOL_ERROR(Err_DoesNotExist);
	}

	// Open the object
	std::auto_ptr<IOStream> object(rContext.OpenObject(mObjectID));

	// Stream it to the peer
	rProtocol.SendStreamAfterCommand(object);

	// Tell the caller what the file was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetFile::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Command to get an file object from the server -- may have to do a bit of
//				 work to get the object.
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetFile::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Check the objects exist
	if(!rContext.ObjectExists(mObjectID)
		|| !rContext.ObjectExists(mInDirectory))
	{
		return PROTOCOL_ERROR(Err_DoesNotExist);
	}

	// Get the directory it's in
	const BackupStoreDirectory &rdir(rContext.GetDirectory(mInDirectory));

	// Find the object within the directory
	BackupStoreDirectory::Entry *pfileEntry = rdir.FindEntryByID(mObjectID);
	if(pfileEntry == 0)
	{
		return PROTOCOL_ERROR(Err_DoesNotExistInDirectory);
	}

	// The result
	std::auto_ptr<IOStream> stream;

	// Does this depend on anything?
	if(pfileEntry->GetDependsNewer() != 0)
	{
		// File exists, but is a patch from a new version. Generate the older version.
		std::vector<int64_t> patchChain;
		int64_t id = mObjectID;
		BackupStoreDirectory::Entry *en = 0;
		do
		{
			patchChain.push_back(id);
			en = rdir.FindEntryByID(id);
			if(en == 0)
			{
				BOX_ERROR("Object " <<
					BOX_FORMAT_OBJECTID(mObjectID) <<
					" in dir " <<
					BOX_FORMAT_OBJECTID(mInDirectory) <<
					" for account " <<
					BOX_FORMAT_ACCOUNT(rContext.GetClientID()) <<
					" references object " <<
					BOX_FORMAT_OBJECTID(id) <<
					" which does not exist in dir");
				return PROTOCOL_ERROR(Err_PatchConsistencyError);
			}
			id = en->GetDependsNewer();
		}
		while(en != 0 && id != 0);

		// OK! The last entry in the chain is the full file, the others are patches back from it.
		// Open the last one, which is the current from file
		std::auto_ptr<IOStream> from(rContext.OpenObject(patchChain[patchChain.size() - 1]));

		// Then, for each patch in the chain, do a combine
		for(int p = ((int)patchChain.size()) - 2; p >= 0; --p)
		{
			// ID of patch
			int64_t patchID = patchChain[p];

			// Open it a couple of times
			std::auto_ptr<IOStream> diff(rContext.OpenObject(patchID));
			std::auto_ptr<IOStream> diff2(rContext.OpenObject(patchID));

			// Choose a temporary filename for the result of the combination
			std::ostringstream fs;
			fs << rContext.GetAccountRoot() << ".recombinetemp." << p;
			std::string tempFn =
				RaidFileController::DiscSetPathToFileSystemPath(
					rContext.GetStoreDiscSet(), fs.str(),
					p + 16);

			// Open the temporary file
			std::auto_ptr<IOStream> combined(
				new InvisibleTempFileStream(
					tempFn, O_RDWR | O_CREAT | O_EXCL |
					O_BINARY | O_TRUNC));

			// Do the combining
			BackupStoreFile::CombineFile(*diff, *diff2, *from, *combined);

			// Move to the beginning of the combined file
			combined->Seek(0, IOStream::SeekType_Absolute);

			// Then shuffle round for the next go
			if (from.get()) from->Close();
			from = combined;
		}

		// Now, from contains a nice file to send to the client. Reorder it
		{
			// Write nastily to allow this to work with gcc 2.x
			std::auto_ptr<IOStream> t(BackupStoreFile::ReorderFileToStreamOrder(from.get(), true /* take ownership */));
			stream = t;
		}

		// Release from file to avoid double deletion
		from.release();
	}
	else
	{
		// Simple case: file already exists on disc ready to go

		// Open the object
		std::auto_ptr<IOStream> object(rContext.OpenObject(mObjectID));
		BufferedStream buf(*object);

		// Verify it
		if(!BackupStoreFile::VerifyEncodedFileFormat(buf))
		{
			return PROTOCOL_ERROR(Err_FileDoesNotVerify);
		}

		// Reset stream -- seek to beginning
		object->Seek(0, IOStream::SeekType_Absolute);

		// Reorder the stream/file into stream order
		{
			// Write nastily to allow this to work with gcc 2.x
			std::auto_ptr<IOStream> t(BackupStoreFile::ReorderFileToStreamOrder(object.get(), true /* take ownership */));
			stream = t;
		}

		// Object will be deleted when the stream is deleted,
		// so can release the object auto_ptr here to avoid
		// premature deletion
		object.release();
	}

	// Stream the reordered stream to the peer
	rProtocol.SendStreamAfterCommand(stream);

	// Tell the caller what the file was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolCreateDirectory::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Create directory command
//		Created: 2003/09/04
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolCreateDirectory::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext,
	IOStream& rDataStream) const
{
	return BackupProtocolCreateDirectory2(mContainingDirectoryID,
		mAttributesModTime, mAttributesModTime /* ModificationTime */,
		mDirectoryName).DoCommand(rProtocol, rContext, rDataStream);
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolCreateDirectory2::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Create directory command, with a specific
//			 modification time.
//		Created: 2014/02/11
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolCreateDirectory2::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext,
	IOStream& rDataStream) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Collect the attributes -- do this now so no matter what the outcome,
	// the data has been absorbed.
	StreamableMemBlock attr;
	attr.Set(rDataStream, rProtocol.GetTimeout());

	// Check to see if the hard limit has been exceeded
	if(rContext.HardLimitExceeded())
	{
		// Won't allow creation if the limit has been exceeded
		return PROTOCOL_ERROR(Err_StorageLimitExceeded);
	}

	bool alreadyExists = false;
	int64_t id = rContext.AddDirectory(mContainingDirectoryID,
		mDirectoryName, attr, mAttributesModTime, mModificationTime,
		alreadyExists);

	if(alreadyExists)
	{
		return PROTOCOL_ERROR(Err_DirectoryAlreadyExists);
	}

	// Tell the caller what the file was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(id));
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolChangeDirAttributes::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Change attributes on directory
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolChangeDirAttributes::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext,
	IOStream& rDataStream) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Collect the attributes -- do this now so no matter what the outcome,
	// the data has been absorbed.
	StreamableMemBlock attr;
	attr.Set(rDataStream, rProtocol.GetTimeout());

	// Get the context to do it's magic
	rContext.ChangeDirAttributes(mObjectID, attr, mAttributesModTime);

	// Tell the caller what the file was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolChangeDirAttributes2::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Change attributes on directory with modification time
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolChangeDirAttributes2::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext,
	IOStream& rDataStream) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Collect the attributes -- do this now so no matter what the outcome,
	// the data has been absorbed.
	StreamableMemBlock attr;
	attr.Set(rDataStream, rProtocol.GetTimeout());

	// Get the context to do it's magic
	rContext.ChangeDirAttributes(mObjectID, attr, mAttributesModTime, mModTime);

	// Tell the caller what the file was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolSetReplacementFileAttributes::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Change attributes on directory
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage>
BackupProtocolSetReplacementFileAttributes::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext,
	IOStream& rDataStream) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Collect the attributes -- do this now so no matter what the outcome,
	// the data has been absorbed.
	StreamableMemBlock attr;
	attr.Set(rDataStream, rProtocol.GetTimeout());

	// Get the context to do it's magic
	int64_t objectID = 0;
	if(!rContext.ChangeFileAttributes(mFilename, mInDirectory, attr, mAttributesHash, objectID))
	{
		// Didn't exist
		return PROTOCOL_ERROR(Err_DoesNotExist);
	}

	// Tell the caller what the file was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(objectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolDeleteFile::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Delete a file
//		Created: 2003/10/21
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolDeleteFile::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Context handles this
	int64_t objectID = 0;
	rContext.DeleteFile(mFilename, mInDirectory, objectID, mFlags, mDeleteFromStore);

	// return the object ID or zero for not found
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(objectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolDeleteFileASAP::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Delete a file. mark it for deletion ASAP
//		Created: 2022/09/08
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolDeleteFileASAP::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Context handles this
	int64_t objectID = 0;
	rContext.DeleteFile(mFilename, mInDirectory, objectID, BackupStoreDirectory::Entry::Flags_RemoveASAP, false);

	// return the object ID or zero for not found
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(objectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolUndeleteFile::DoCommand(
//			 BackupProtocolBase &, BackupStoreContext &)
//		Purpose: Undelete a file
//		Created: 2008-09-12
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolUndeleteFile::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Context handles this
	bool result = rContext.UndeleteFile(mObjectID, mInDirectory);

	// return the object ID or zero for not found
	return std::auto_ptr<BackupProtocolMessage>(
		new BackupProtocolSuccess(result ? mObjectID : 0));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolDeleteDirectory::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Delete a directory, mark it for deletion ASAP
//		Created: 2022/09/08
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolDeleteDirectoryASAP::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Check it's not asking for the root directory to be deleted
	if(mObjectID == BACKUPSTORE_ROOT_DIRECTORY_ID)
	{
		return PROTOCOL_ERROR(Err_CannotDeleteRoot);
	}

	// Context handles this
	rContext.DeleteDirectory(mObjectID, false, BackupStoreDirectory::Entry::Flags_RemoveASAP);

	// return the object ID
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolDeleteDirectory::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Delete a directory
//		Created: 2003/10/21
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolDeleteDirectory::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Check it's not asking for the root directory to be deleted
	if(mObjectID == BACKUPSTORE_ROOT_DIRECTORY_ID)
	{
		return PROTOCOL_ERROR(Err_CannotDeleteRoot);
	}

	// Context handles this
	rContext.DeleteDirectory(mObjectID, false, mFlags, mDeleteFromStore);

	// return the object ID
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolUndeleteDirectory::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Undelete a directory
//		Created: 23/11/03
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolUndeleteDirectory::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Check it's not asking for the root directory to be deleted
	if(mObjectID == BACKUPSTORE_ROOT_DIRECTORY_ID)
	{
		return PROTOCOL_ERROR(Err_CannotDeleteRoot);
	}

	// Context handles this
	rContext.DeleteDirectory(mObjectID, true /* undelete */);

	// return the object ID
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolSetClientStoreMarker::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Command to set the client's store marker
//		Created: 2003/10/29
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolSetClientStoreMarker::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Set the marker
	rContext.SetClientStoreMarker(mClientStoreMarker);

	// return store marker set
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mClientStoreMarker));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolMoveObject::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Command to move an object from one directory to another
//		Created: 2003/11/12
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolMoveObject::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Let context do this, but modify error reporting on exceptions...
	rContext.MoveObject(mObjectID, mMoveFromDirectory, mMoveToDirectory,
		mNewFilename, (mFlags & Flags_MoveAllWithSameName) == Flags_MoveAllWithSameName,
		(mFlags & Flags_AllowMoveOverDeletedObject) == Flags_AllowMoveOverDeletedObject);

	// Return the object ID
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetObjectInfos::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Command to gather some infos from an object
//		Created: 24/01/26
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetObjectInfos::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
		CHECK_PHASE(Phase_Commands)
		bool isDir;
		int64_t containerID;
		rContext.GetObjectInfos(mObjectID, isDir, containerID);

		return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolObjectInfos(isDir, containerID));

}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetObjectName2::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Command to find the name of an object
//		Created: 24/01/26
//
// --------------------------------------------------------------------------

std::auto_ptr<BackupProtocolMessage> BackupProtocolGetObjectName2::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Create a stream for the list of filenames
	std::auto_ptr<CollectInBufferStream> stream(new CollectInBufferStream);

	// Object and directory IDs
	int64_t objectID = mObjectID;
	int64_t dirID = mContainingDirectoryID;

	// Data to return in the reply
	int32_t numNameElements = 0;
	int16_t objectFlags = 0;
	int64_t modTime = 0;
	uint64_t attrModHash = 0;
	int64_t backupTime = 0;
	int64_t deleteTime = 0;
	bool haveModTimes = false;

	do
	{
		// Check the directory really exists
		if(!rContext.ObjectExists(dirID, BackupStoreContext::ObjectExists_Directory))
		{
			return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolObjectName2(BackupProtocolObjectName2::NumNameElements_ObjectDoesntExist, 0, 0, 0, 0, 0));
		}

		// Load up the directory
		const BackupStoreDirectory *pDir;

		try
		{
			pDir = &rContext.GetDirectory(dirID);
		}
		catch(BackupStoreException &e)
		{
			if(e.GetSubType() == BackupStoreException::ObjectDoesNotExist)
			{
				// If this can't be found, then there is a problem...
				// tell the caller it can't be found.
				return std::auto_ptr<BackupProtocolMessage>(
					new BackupProtocolObjectName2(
						BackupProtocolObjectName2::NumNameElements_ObjectDoesntExist,
						0, 0, 0, 0, 0));
			}

			throw;
		}

		const BackupStoreDirectory& rdir(*pDir);

		// Find the element in this directory and store it's name
		if(objectID != ObjectID_DirectoryOnly)
		{
			const BackupStoreDirectory::Entry *en = rdir.FindEntryByID(objectID);

			// If this can't be found, then there is a problem... tell the caller it can't be found
			if(en == 0)
			{
				// Abort!
				return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolObjectName2(BackupProtocolObjectName2::NumNameElements_ObjectDoesntExist, 0, 0, 0, 0, 0));
			}

			// Store flags?
			if(objectFlags == 0)
			{
				objectFlags = en->GetFlags();
			}

			// Store modification times?
			if(!haveModTimes)
			{
				modTime = en->GetModificationTime();
				attrModHash = en->GetAttributesHash();
				backupTime = en->GetBackupTime();
				deleteTime = en->GetDeleteTime();
				haveModTimes = true;
			}

			// Store the name in the stream
			en->GetName().WriteToStream(*stream);

			// Count of name elements
			++numNameElements;
		}

		// Setup for next time round
		objectID = dirID;
		dirID = rdir.GetContainerID();

	} while(objectID != 0 && objectID != BACKUPSTORE_ROOT_DIRECTORY_ID);

	// Stream to send?
	if(numNameElements > 0)
	{
		// Get the stream ready to go
		stream->SetForReading();
		// Tell the protocol to send the stream
		rProtocol.SendStreamAfterCommand(static_cast< std::auto_ptr<IOStream> >(stream));
	}


	// Make reply
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolObjectName2(numNameElements, modTime, attrModHash, backupTime, deleteTime, objectFlags));
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetObjectName::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Command to find the name of an object
//		Created: 12/11/03
//
// --------------------------------------------------------------------------

std::auto_ptr<BackupProtocolMessage> BackupProtocolGetObjectName::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)




	// Create a stream for the list of filenames
	std::auto_ptr<CollectInBufferStream> stream(new CollectInBufferStream);

	// Object and directory IDs
	int64_t objectID = mObjectID;
	int64_t dirID = mContainingDirectoryID;

	// Data to return in the reply
	int32_t numNameElements = 0;
	int16_t objectFlags = 0;
	int64_t modTime = 0;
	uint64_t attrModHash = 0;

	bool haveModTimes = false;

	do
	{
		// Check the directory really exists
		if(!rContext.ObjectExists(dirID, BackupStoreContext::ObjectExists_Directory))
		{
			return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolObjectName(BackupProtocolObjectName::NumNameElements_ObjectDoesntExist, 0, 0, 0));
		}

		// Load up the directory
		const BackupStoreDirectory *pDir;

		try
		{
			pDir = &rContext.GetDirectory(dirID);
		}
		catch(BackupStoreException &e)
		{
			if(e.GetSubType() == BackupStoreException::ObjectDoesNotExist)
			{
				// If this can't be found, then there is a problem...
				// tell the caller it can't be found.
				return std::auto_ptr<BackupProtocolMessage>(
					new BackupProtocolObjectName(
						BackupProtocolObjectName::NumNameElements_ObjectDoesntExist,
						0, 0, 0));
			}

			throw;
		}

		const BackupStoreDirectory& rdir(*pDir);

		// Find the element in this directory and store it's name
		if(objectID != ObjectID_DirectoryOnly)
		{
			const BackupStoreDirectory::Entry *en = rdir.FindEntryByID(objectID);

			// If this can't be found, then there is a problem... tell the caller it can't be found
			if(en == 0)
			{
				// Abort!
				return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolObjectName(BackupProtocolObjectName::NumNameElements_ObjectDoesntExist, 0, 0, 0));
			}

			// Store flags?
			if(objectFlags == 0)
			{
				objectFlags = en->GetFlags();
			}

			// Store modification times?
			if(!haveModTimes)
			{
				modTime = en->GetModificationTime();
				attrModHash = en->GetAttributesHash();
				haveModTimes = true;
			}

			// Store the name in the stream
			en->GetName().WriteToStream(*stream);

			// Count of name elements
			++numNameElements;
		}

		// Setup for next time round
		objectID = dirID;
		dirID = rdir.GetContainerID();

	} while(objectID != 0 && objectID != BACKUPSTORE_ROOT_DIRECTORY_ID);

	// Stream to send?
	if(numNameElements > 0)
	{
		// Get the stream ready to go
		stream->SetForReading();
		// Tell the protocol to send the stream
		rProtocol.SendStreamAfterCommand(static_cast< std::auto_ptr<IOStream> >(stream));
	}


	// Make reply
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolObjectName(numNameElements, modTime, attrModHash, objectFlags));
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetBlockIndexByID::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Get the block index from a file, by ID
//		Created: 19/1/04
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetBlockIndexByID::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Open the file
	std::auto_ptr<IOStream> stream(rContext.OpenObject(mObjectID));

	// Move the file pointer to the block index
	BackupStoreFile::MoveStreamPositionToBlockIndex(*stream);

	// Return the stream to the client
	rProtocol.SendStreamAfterCommand(stream);

	// Return the object ID
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetBlockIndexByName::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Get the block index from a file, by name within a directory
//		Created: 19/1/04
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetBlockIndexByName::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Get the directory
	const BackupStoreDirectory &dir(rContext.GetDirectory(mInDirectory));

	// Find the latest object ID within it which has the same name
	int64_t objectID = 0;
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en = 0;
	while((en = i.Next(BackupStoreDirectory::Entry::Flags_File)) != 0)
	{
		if(en->GetName() == mFilename)
		{
			// Store the ID, if it's a newer ID than the last one
			if(en->GetObjectID() > objectID)
			{
				objectID = en->GetObjectID();
			}
		}
	}

	// Found anything?
	if(objectID == 0)
	{
		// No... return a zero object ID
		return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(0));
	}

	// Open the file
	std::auto_ptr<IOStream> stream(rContext.OpenObject(objectID));

	// Move the file pointer to the block index
	BackupStoreFile::MoveStreamPositionToBlockIndex(*stream);

	// Return the stream to the client
	rProtocol.SendStreamAfterCommand(stream);

	// Return the object ID
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(objectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetAccountUsage::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Return the amount of disc space used
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetAccountUsage::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Get store info from context
	const BackupStoreInfo &rinfo(rContext.GetBackupStoreInfo());

	// Find block size
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet &rdiscSet(rcontroller.GetDiscSet(rinfo.GetDiscSetNumber()));

	// Return info
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolAccountUsage(
		rinfo.GetBlocksUsed(),
		rinfo.GetBlocksInOldFiles(),
		rinfo.GetBlocksInDeletedFiles(),
		rinfo.GetBlocksInDirectories(),
		rinfo.GetBlocksSoftLimit(),
		rinfo.GetBlocksHardLimit(),
		rdiscSet.GetBlockSize()
	));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolListBackups::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Return the Backups list
//		Created: 2023/11/02
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolListBackups::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Open the file
	std::auto_ptr<IOStream> stream(BackupsList::OpenStream(RaidFileController::DiscSetPathToFileSystemPath(rContext.GetStoreDiscSet(), rContext.GetAccountRoot(), 1)));
	rProtocol.SendStreamAfterCommand(stream);
	
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolBackups());

}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetIsAlive::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Return the amount of disc space used
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetIsAlive::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	//
	// NOOP
	//
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolIsAlive());
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetAccountUsage2::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Return the amount of disc space used
//		Created: 26/12/13
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetAccountUsage2::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Get store info from context
	const BackupStoreInfo &info(rContext.GetBackupStoreInfo());

	// Find block size
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet &rdiscSet(rcontroller.GetDiscSet(info.GetDiscSetNumber()));

	// Return info
	BackupProtocolAccountUsage2* usage = new BackupProtocolAccountUsage2();
	std::auto_ptr<BackupProtocolMessage> reply(usage);
	#define COPY(name) usage->Set ## name (info.Get ## name ())
	COPY(AccountName);
	usage->SetAccountEnabled(info.IsAccountEnabled());
	COPY(ClientStoreMarker);
	usage->SetBlockSize(rdiscSet.GetBlockSize());
	COPY(LastObjectIDUsed);
	COPY(BlocksUsed);
	COPY(BlocksInCurrentFiles);
	COPY(BlocksInOldFiles);
	COPY(BlocksInDeletedFiles);
	COPY(BlocksInDirectories);
	COPY(BlocksSoftLimit);
	COPY(BlocksHardLimit);
	COPY(NumCurrentFiles);
	COPY(NumOldFiles);
	COPY(NumDeletedFiles);
	COPY(NumDirectories);
	#undef COPY

	return reply;
}
