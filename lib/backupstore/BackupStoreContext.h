// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreContext.h
//		Purpose: Context for backup store server
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#ifndef BACKUPCONTEXT__H
#define BACKUPCONTEXT__H

#include <string>
#include <map>
#include <memory>
#include <iostream>

#include "autogen_BackupProtocol.h"
#include "BackupStoreInfo.h"
#include "BackupStoreRefCountDatabase.h"
#include "NamedLock.h"
#include "Message.h"
#include "Utils.h"
#include "Archive.h"


class BackupStoreDirectory;
class BackupStoreFilename;
class IOStream;
class BackupProtocolMessage;
class StreamableMemBlock;

class HousekeepingInterface
{
	public:
	virtual ~HousekeepingInterface() { }
	virtual void SendMessageToHousekeepingProcess(const void *Msg, int MsgLen) = 0;
};

class SessionInfos {
	public:
		SessionInfos() {
			mAddedFilesCount = 0;
			mAddedFilesBlocksCount = 0;
			mDeletedFilesCount = 0;
			mDeletedFilesBlocksCount = 0;
			mAddedDirectoriesCount = 0;
			mDeletedDirectoriesCount = 0;

			mStartTime = GetCurrentBoxTime();
			mEndTime = 0;
		}


		bool operator<(const SessionInfos& other) const {
			return GetStartTime() < other.GetStartTime();
		}
		
		bool operator<(const box_time_t other) const {
			return GetStartTime() < other;
		}

		box_time_t ElapsedTime() { return GetCurrentBoxTime() - mStartTime; }
		const box_time_t GetStartTime() const { return mStartTime; }
		const box_time_t GetEndTime() const { return mEndTime; }
		void SetStart(box_time_t time = 0) { 
			if( time==0 ) 
			{
				mStartTime = GetCurrentBoxTime();
			} else 
			{
				mStartTime = time;
			} 
		}

		void SetEnd(box_time_t time = 0) { 
			if( time==0 ) 
			{
				mEndTime = GetCurrentBoxTime();
			} else 
			{
				mEndTime = time;
			} 
		}

		void RecordFileAdded(uint64_t blocks) 
		{ 
			mAddedFilesBlocksCount += blocks; 
			mAddedFilesCount++;
		}
		
		void RecordFileDeleted(uint64_t blocks) 
		{ 
			mDeletedFilesBlocksCount += blocks; 
			mDeletedFilesCount++;
		}

		void RecordDirectoryAdded() { mAddedDirectoriesCount++; }
		void RecordDirectoryDeleted() { mDeletedDirectoriesCount++; }

		
		const uint64_t GetAddedFilesCount() const { return mAddedFilesCount; }
		void SetAddedFilesCount(uint64_t count) { mAddedFilesCount = count; }
		const uint64_t GetAddedFilesBlocksCount() const { return mAddedFilesBlocksCount; }
		void SetAddedFilesBlocksCount(uint64_t count) { mAddedFilesBlocksCount = count; }
		const uint64_t GetDeletedFilesCount() const { return mDeletedFilesCount; }
		void SetDeletedFilesCount(uint64_t count) { mDeletedFilesCount = count; }
		const uint64_t GetDeletedFilesBlocksCount() const { return mDeletedFilesBlocksCount; }
		void SetDeletedFilesBlocksCount(uint64_t count) { mDeletedFilesBlocksCount = count; }
		const uint64_t GetAddedDirectoriesCount() const { return mAddedDirectoriesCount; }
		void SetAddedDirectoriesCount(uint64_t count) { mAddedDirectoriesCount = count; }
		const uint64_t GetDeletedDirectoriesCount() const { return mDeletedDirectoriesCount; }
		void SetDeletedDirectoriesCount(uint64_t count) { mDeletedDirectoriesCount = count; }


		void WriteToStream(IOStream &Stream, int Timeout = IOStream::TimeOutInfinite)
		{
			Archive ar(Stream, Timeout);
			ar.Write(mStartTime);
			ar.Write(mEndTime);
			ar.Write(mAddedFilesCount);
			ar.Write(mAddedFilesBlocksCount);
			ar.Write(mDeletedFilesCount);
			ar.Write(mDeletedFilesBlocksCount);
			ar.Write(mAddedDirectoriesCount);
			ar.Write(mDeletedDirectoriesCount);
		}

		void ReadFromStream(IOStream &Stream, int Timeout = IOStream::TimeOutInfinite)
		{
			Archive ar(Stream, Timeout);
			ar.Read(mStartTime);
			ar.Read(mEndTime);
			ar.Read(mAddedFilesCount);
			ar.Read(mAddedFilesBlocksCount);
			ar.Read(mDeletedFilesCount);
			ar.Read(mDeletedFilesBlocksCount);
			ar.Read(mAddedDirectoriesCount);
			ar.Read(mDeletedDirectoriesCount);
		}

		void Dump() 
		{
			std::cout << "SessionInfos: " << std::endl;
			std::cout << "  StartTime: " << mStartTime << std::endl;
			std::cout << "  EndTime: " << mEndTime << std::endl;
			std::cout << "  AddedFilesCount: " << mAddedFilesCount << std::endl;
			std::cout << "  AddedFilesBlocksCount: " << mAddedFilesBlocksCount << std::endl;
			std::cout << "  DeletedFilesCount: " << mDeletedFilesCount << std::endl;
			std::cout << "  DeletedFilesBlocksCount: " << mDeletedFilesBlocksCount << std::endl;
			std::cout << "  AddedDirectoriesCount: " << mAddedDirectoriesCount << std::endl;
			std::cout << "  DeletedDirectoriesCount: " << mDeletedDirectoriesCount << std::endl;
		}

		bool HasChanges() {
			return (mAddedFilesCount > 0 || mAddedDirectoriesCount > 0 || mDeletedFilesCount > 0 || mDeletedDirectoriesCount > 0);
		}

	private:
		box_time_t mStartTime;
		box_time_t mEndTime;
		uint64_t mAddedFilesCount;
		uint64_t mAddedFilesBlocksCount;
		uint64_t mDeletedFilesCount;
		uint64_t mDeletedFilesBlocksCount;
		uint64_t mAddedDirectoriesCount;
		uint64_t mDeletedDirectoriesCount;

};


// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreContext
//		Purpose: Context for backup store server
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
class BackupStoreContext
{
public:
	BackupStoreContext(int32_t ClientID,
		HousekeepingInterface* mpHousekeeping,
		const std::string& rConnectionDetails);
	~BackupStoreContext();
private:
	BackupStoreContext(const BackupStoreContext &rToCopy);
public:

	void ReceivedFinishCommand();
	void CleanUp();

	enum
	{
		Phase_START		= 0,
		Phase_Version	= 0,
		Phase_Login		= 1,
		Phase_Commands	= 2
	};

	int GetPhase() const {return mProtocolPhase;}
	std::string GetPhaseName() const
	{
		switch(mProtocolPhase)
		{
			case Phase_Version:  return "Phase_Version";
			case Phase_Login:    return "Phase_Login";
			case Phase_Commands: return "Phase_Commands";
			default:
				std::ostringstream oss;
				oss << "Unknown phase " << mProtocolPhase;
				return oss.str();
		}
	}
	void SetPhase(int NewPhase) {mProtocolPhase = NewPhase;}

	// Read only locking
	bool SessionIsReadOnly() {return mReadOnly;}
	bool AttemptToGetWriteLock();


	// Not really an API, but useful for BackupProtocolLocal2.
	void ReleaseWriteLock()
	{
		if(mWriteLock.GotLock())
		{
			mWriteLock.ReleaseLock();
		}
	}

	void SetClientHasAccount(const std::string &rStoreRoot, int StoreDiscSet) {mClientHasAccount = true; mAccountRootDir = rStoreRoot; mStoreDiscSet = StoreDiscSet;}
	bool GetClientHasAccount() const {return mClientHasAccount;}
	const std::string &GetAccountRoot() const {return mAccountRootDir;}
	int GetStoreDiscSet() const {return mStoreDiscSet;}

	// Store info
	void LoadStoreInfo();
	void SaveStoreInfo(bool AllowDelay = true);
	const BackupStoreInfo &GetBackupStoreInfo() const;
	const std::string GetAccountName()
	{
		if(!mapStoreInfo.get())
		{
			return "Unknown";
		}
		return mapStoreInfo->GetAccountName();
	}

	// Client marker
	int64_t GetClientStoreMarker();
	void SetClientStoreMarker(int64_t ClientStoreMarker);

	// Usage information
	void GetStoreDiscUsageInfo(int64_t &rBlocksUsed, int64_t &rBlocksSoftLimit, int64_t &rBlocksHardLimit);
	bool HardLimitExceeded();

	// Reading directories
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupStoreContext::GetDirectory(int64_t)
	//		Purpose: Return a reference to a directory. Valid only until the 
	//				 next time a function which affects directories is called.
	//				 Mainly this funciton, and creation of files.
	//		Created: 2003/09/02
	//
	// --------------------------------------------------------------------------
	const BackupStoreDirectory &GetDirectory(int64_t ObjectID)
	{
		// External callers aren't allowed to change it -- this function
		// merely turns the returned directory const.
		return GetDirectoryInternal(ObjectID);
	}

	// Manipulating files/directories
	int64_t AddFile(IOStream &rFile,
		int64_t InDirectory,
		int64_t ModificationTime,
		int64_t AttributesHash,
		int64_t DiffFromFileID,
		const BackupStoreFilename &rFilename,
		bool MarkFileWithSameNameAsOldVersions,
		uint64_t ResumeOffset);
	int64_t AddDirectory(int64_t InDirectory,
		const BackupStoreFilename &rFilename,
		const StreamableMemBlock &Attributes,
		int64_t AttributesModTime,
		int64_t ModificationTime,
		bool &rAlreadyExists);
	void ChangeDirAttributes(int64_t Directory, const StreamableMemBlock &Attributes, int64_t AttributesModTime, int64_t ModificationTime = 0);
	bool ChangeFileAttributes(const BackupStoreFilename &rFilename, int64_t InDirectory, const StreamableMemBlock &Attributes, int64_t AttributesHash, int64_t &rObjectIDOut);
	bool DeleteFile(const BackupStoreFilename &rFilename, int64_t InDirectory, int64_t &rObjectIDOut, uint16_t Flags = 0, bool DeleteFromStore = false);
	bool UndeleteFile(int64_t ObjectID, int64_t InDirectory);
	void DeleteDirectory(int64_t ObjectID, bool Undelete = false, uint16_t Flags = 0, bool DeleteFromStore = false);
	void MoveObject(int64_t ObjectID, int64_t MoveFromDirectory, int64_t MoveToDirectory, const BackupStoreFilename &rNewFilename, bool MoveAllWithSameName, bool AllowMoveOverDeletedObject);

	// Manipulating objects
	enum
	{
		ObjectExists_Anything = 0,
		ObjectExists_File = 1,
		ObjectExists_Directory = 2
	};
	bool ObjectExists(int64_t ObjectID, int MustBe = ObjectExists_Anything);
	std::auto_ptr<IOStream> OpenObject(int64_t ObjectID);
	void GetObjectInfos(int64_t ObjectID, bool &rIsDirectory, int64_t &rContainerID);
	
	// Info
	int32_t GetClientID() const {return mClientID;}
	const std::string& GetConnectionDetails() { return mConnectionDetails; }

	void SetProtocolVersion(int32_t ProtocolVersion) {mProtocolVersion = ProtocolVersion;}
	int32_t GetProtocolVersion() const {return mProtocolVersion;}

	SessionInfos &GetSessionInfos() { return mSessionInfos; }
	box_time_t GetSessionStartTime() { return mSessionInfos.GetStartTime(); }

private:
	void MakeObjectFilename(int64_t ObjectID, std::string &rOutput, bool EnsureDirectoryExists = false);
	BackupStoreDirectory &GetDirectoryInternal(int64_t ObjectID,
		bool AllowFlushCache = true);
	void SaveDirectory(BackupStoreDirectory &rDir);
	void RemoveDirectoryFromCache(int64_t ObjectID);
	void ClearDirectoryCache();
	void DeleteDirectoryRecurse(int64_t ObjectID, bool Undelete, uint16_t Flags = 0, bool DeleteFromStore = false);
	int64_t AllocateObjectID();

	std::string mConnectionDetails;
	int32_t mClientID;
	int32_t mProtocolVersion;
	HousekeepingInterface *mpHousekeeping;
	int mProtocolPhase;
	bool mClientHasAccount;
	std::string mAccountRootDir;	// has final directory separator
	int mStoreDiscSet;

	bool mReadOnly;
	NamedLock mWriteLock;
	int mSaveStoreInfoDelay; // how many times to delay saving the store info
	
	// Store info
	std::auto_ptr<BackupStoreInfo> mapStoreInfo;

	// Refcount database
	std::auto_ptr<BackupStoreRefCountDatabase> mapRefCount;

	// Directory cache
	std::map<int64_t, BackupStoreDirectory*> mDirectoryCache;

	// SessionInfos
	SessionInfos mSessionInfos;
public:
	class TestHook
	{
		public:
		virtual std::auto_ptr<BackupProtocolMessage>
			StartCommand(const BackupProtocolMessage& rCommand) = 0;
		virtual ~TestHook() { }
	};
	void SetTestHook(TestHook& rTestHook)
	{
		mpTestHook = &rTestHook;
	}
	std::auto_ptr<BackupProtocolMessage>
		StartCommandHook(const BackupProtocolMessage& rCommand)
	{
		if(mpTestHook)
		{
			return mpTestHook->StartCommand(rCommand);
		}
		return std::auto_ptr<BackupProtocolMessage>();
	}

private:
	TestHook* mpTestHook;
};

#endif // BACKUPCONTEXT__H

