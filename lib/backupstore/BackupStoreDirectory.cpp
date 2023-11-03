// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreDirectory.cpp
//		Purpose: Representation of a backup directory
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/types.h>
#include <map>
#include "BackupConstants.h"
#include "BackupStoreDirectory.h"
#include "IOStream.h"
#include "BackupStoreException.h"
#include "BackupStoreObjectMagic.h"

#include "MemLeakFindOn.h"

// set packing to one byte
#ifdef STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#include "BeginStructPackForWire.h"
#else
BEGIN_STRUCTURE_PACKING_FOR_WIRE
#endif

typedef struct
{
	int32_t mMagicValue;	// also the version number
	int32_t mNumEntries;
	int64_t mObjectID;		// this object ID
	int64_t mContainerID;	// ID of container
	uint64_t mAttributesModTime;
	int32_t mOptionsPresent;	// bit mask of optional sections / features present
	// Then a StreamableMemBlock for attributes
} dir_StreamFormat;

typedef struct
{
	uint64_t mModificationTime;
	int64_t mObjectID;
	int64_t mSizeInBlocks;
	uint64_t mAttributesHash;
	int16_t mFlags;				// order smaller items after bigger ones (for alignment)
	// Then a BackupStoreFilename
	// Then a StreamableMemBlock for attributes
	// Then the backup time
} en_StreamFormat;

typedef struct
{
	int64_t mDependsNewer;
	int64_t mDependsOlder;
} en_StreamFormatDepends;

// Use default packing
#ifdef STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#include "EndStructPackForWire.h"
#else
END_STRUCTURE_PACKING_FOR_WIRE
#endif


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::BackupStoreDirectory()
//		Purpose: Constructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreDirectory::BackupStoreDirectory()
:
#ifndef BOX_RELEASE_BUILD
  mInvalidated(false),
#endif
  mRevisionID(0),
  mObjectID(0),
  mContainerID(0),
  mAttributesModTime(0),
  mUserInfo1(0)
{
	ASSERT(sizeof(uint64_t) == sizeof(box_time_t));
}


// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreDirectory::BackupStoreDirectory(int64_t, int64_t)
//		Purpose: Constructor giving object and container IDs
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreDirectory::BackupStoreDirectory(int64_t ObjectID, int64_t ContainerID)
:
#ifndef BOX_RELEASE_BUILD
  mInvalidated(false),
#endif
  mRevisionID(0),
  mObjectID(ObjectID),
  mContainerID(ContainerID),
  mAttributesModTime(0),
  mUserInfo1(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::~BackupStoreDirectory()
//		Purpose: Destructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreDirectory::~BackupStoreDirectory()
{
	for(std::vector<Entry*>::iterator i(mEntries.begin()); i != mEntries.end(); ++i)
	{
		delete (*i);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::ReadFromStream(IOStream &, int)
//		Purpose: Reads the directory contents from a stream.
//			 Exceptions will result in incomplete reads.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::ReadFromStream(IOStream &rStream, int Timeout)
{
	ASSERT(!mInvalidated); // Compiled out of release builds
	// Get the header
	dir_StreamFormat hdr;
	if(!rStream.ReadFullBuffer(&hdr, sizeof(hdr), 0 /* not interested in bytes read if this fails */, Timeout))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}

	// Check magic value...
	uint32_t magicValue = ntohl(hdr.mMagicValue);
	if(magicValue != OBJECTMAGIC_DIR_MAGIC_VALUE_V0 &&
		magicValue != OBJECTMAGIC_DIR_MAGIC_VALUE_V1)
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, BadDirectoryFormat,
			"Wrong magic number for directory: expected " <<
			BOX_FORMAT_HEX32(OBJECTMAGIC_DIR_MAGIC_VALUE_V0) <<
			" or " <<
			BOX_FORMAT_HEX32(OBJECTMAGIC_DIR_MAGIC_VALUE_V1) <<
			" but found " <<
			BOX_FORMAT_HEX32(ntohl(hdr.mMagicValue)) << " in " <<
			rStream.ToString());
	}

	// Get data
	mObjectID = box_ntoh64(hdr.mObjectID);
	mContainerID = box_ntoh64(hdr.mContainerID);
	mAttributesModTime = box_ntoh64(hdr.mAttributesModTime);

	// Options
	int32_t options = ntohl(hdr.mOptionsPresent);

	// Get attributes
	mAttributes.ReadFromStream(rStream, Timeout);

	// Decode count
	int count = ntohl(hdr.mNumEntries);

	// Clear existing list
	for(std::vector<Entry*>::iterator i = mEntries.begin();
		i != mEntries.end(); i++)
	{
		delete (*i);
	}
	mEntries.clear();

	// Read them in!
	for(int c = 0; c < count; ++c)
	{
		Entry *pen = new Entry;
		try
		{
			// Read from stream
			pen->ReadFromStream(rStream, Timeout, magicValue);

			// Add to list
			mEntries.push_back(pen);
		}
		catch(...)
		{
			delete pen;
			throw;
		}
	}

	// Read in dependency info?
	if(options & Option_DependencyInfoPresent)
	{
		// Read in extra dependency data
		for(int c = 0; c < count; ++c)
		{
			mEntries[c]->ReadFromStreamDependencyInfo(rStream, Timeout);
		}
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::WriteToStream(IOStream &, int16_t, int16_t, bool, bool)
//		Purpose: Writes a selection of entries to a stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
#include <iostream>
#include "autogen_BackupProtocol.h"

void BackupStoreDirectory::WriteToStream(IOStream &rStream, int16_t FlagsMustBeSet, int16_t FlagsNotToBeSet, box_time_t PointInTime, bool StreamAttributes, bool StreamDependencyInfo, uint32_t ProtocolVersion) const
{
	ASSERT(!mInvalidated); // Compiled out of release builds
	

	std::multimap<std::string, BackupStoreDirectory::Entry*> entries;
 
	// If we're travelling in time we won't filter old or deleted objects
 	if( PointInTime != 0 )
	{
		FlagsNotToBeSet &= ~BackupProtocolListDirectory::Flags_OldVersion;
		FlagsNotToBeSet &= ~BackupProtocolListDirectory::Flags_Deleted;
	}

	Entry *pen = 0;
	Iterator i(*this);
	while((pen = i.Next(FlagsMustBeSet, FlagsNotToBeSet)) != 0)
	{
		if ( PointInTime != 0 ) {
			if( pen->GetBackupTime() <= PointInTime)  {
				if( auto res = entries.find(pen->mName.GetEncodedFilename()); res != entries.end() ) {
					if ( res->second->mModificationTime < pen->mModificationTime ) {
						res->second = pen;
					}
				} else {
					entries.insert({pen->mName.GetEncodedFilename(),  pen});	
				}			
			}
		} else {
			entries.insert({pen->mName.GetEncodedFilename(), pen});
		}
	}
	
	// Check that sensible IDs have been set
	ASSERT(mObjectID != 0);
	ASSERT(mContainerID != 0);

	// Need dependency info?
	bool dependencyInfoRequired = false;
	if(StreamDependencyInfo)
	{
		for ( auto local_it = entries.cbegin(); local_it!= entries.cend(); ++local_it ) {
			Entry *pen = local_it->second;
			if(pen->HasDependencies())
			{
				dependencyInfoRequired = true;
				break;
			}
		}
	}

	// Options
	int32_t options = 0;
	if(dependencyInfoRequired) options |= Option_DependencyInfoPresent;

	// Build header
	dir_StreamFormat hdr;
	if( ProtocolVersion == PROTOCOL_VERSION_V1 ) {
		hdr.mMagicValue = htonl(OBJECTMAGIC_DIR_MAGIC_VALUE_V0);
	} else {
		hdr.mMagicValue = htonl(OBJECTMAGIC_DIR_MAGIC_VALUE_V1);
	}
	// hdr.mMagicValue = htonl(OBJECTMAGIC_DIR_MAGIC_VALUE_V1);
	hdr.mNumEntries = htonl(entries.size());
	hdr.mObjectID = box_hton64(mObjectID);
	hdr.mContainerID = box_hton64(mContainerID);
	hdr.mAttributesModTime = box_hton64(mAttributesModTime);
	hdr.mOptionsPresent = htonl(options);

	// Write header
	rStream.Write(&hdr, sizeof(hdr));

	// Write the attributes?
	if(StreamAttributes)
	{
		mAttributes.WriteToStream(rStream);
	}
	else
	{
		// Write a blank header instead
		StreamableMemBlock::WriteEmptyBlockToStream(rStream);
	}

	// Then write all the entries
	for ( auto local_it = entries.cbegin(); local_it!= entries.cend(); ++local_it ) {
		Entry *pen = local_it->second;
		std::cout << "protocol "<< ProtocolVersion << std::endl;
		pen->WriteToStream(rStream, ProtocolVersion < PROTOCOL_VERSION_V2);
	}
	

	// Write dependency info?
	if(dependencyInfoRequired)
	{
		for ( auto local_it = entries.cbegin(); local_it!= entries.cend(); ++local_it ) {
			Entry *pen = local_it->second;
			pen->WriteToStreamDependencyInfo(rStream);
		}
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::AddEntry(const Entry &)
//		Purpose: Adds entry to directory (no checking)
//		Created: 2003/08/27
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry *BackupStoreDirectory::AddEntry(const Entry &rEntryToCopy)
{
	ASSERT(!mInvalidated); // Compiled out of release builds
	Entry *pnew = new Entry(rEntryToCopy);
	try
	{
		mEntries.push_back(pnew);
	}
	catch(...)
	{
		delete pnew;
		throw;
	}

	return pnew;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::AddEntry(const BackupStoreFilename &, int64_t, int64_t, int16_t)
//		Purpose: Adds entry to directory (no checking)
//		Created: 2003/08/27
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry *
BackupStoreDirectory::AddEntry(const BackupStoreFilename &rName,
	box_time_t ModificationTime, box_time_t BackupTime, int64_t ObjectID, int64_t SizeInBlocks,
	int16_t Flags, uint64_t AttributesHash)
{
	ASSERT(!mInvalidated); // Compiled out of release builds
	Entry *pnew = new Entry(rName, ModificationTime, BackupTime, ObjectID,
		SizeInBlocks, Flags, AttributesHash);
	try
	{
		mEntries.push_back(pnew);
	}
	catch(...)
	{
		delete pnew;
		throw;
	}

	return pnew;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::DeleteEntry(int64_t)
//		Purpose: Deletes entry with given object ID (uses linear search, maybe a little inefficient)
//		Created: 2003/08/27
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::DeleteEntry(int64_t ObjectID)
{
	ASSERT(!mInvalidated); // Compiled out of release builds
	for(std::vector<Entry*>::iterator i(mEntries.begin());
		i != mEntries.end(); ++i)
	{
		if((*i)->mObjectID == ObjectID)
		{
			// Delete
			delete (*i);
			// Remove from list
			mEntries.erase(i);
			// Done
			return;
		}
	}

	// Not found
	THROW_EXCEPTION_MESSAGE(BackupStoreException, CouldNotFindEntryInDirectory,
		"Failed to find entry " << BOX_FORMAT_OBJECTID(ObjectID) <<
		" in directory " << BOX_FORMAT_OBJECTID(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::FindEntryByID(int64_t)
//		Purpose: Finds a specific entry. Returns 0 if the entry doesn't exist.
//		Created: 12/11/03
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry *BackupStoreDirectory::FindEntryByID(int64_t ObjectID) const
{
	ASSERT(!mInvalidated); // Compiled out of release builds
	for(std::vector<Entry*>::const_iterator i(mEntries.begin());
		i != mEntries.end(); ++i)
	{
		if((*i)->mObjectID == ObjectID)
		{
			// Found
			return (*i);
		}
	}

	// Not found
	return 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::Entry()
//		Purpose: Constructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry::Entry()
:
#ifndef BOX_RELEASE_BUILD
  mInvalidated(false),
#endif
  mModificationTime(0),
  mBackupTime(0),
  mObjectID(0),
  mSizeInBlocks(0),
  mFlags(0),
  mAttributesHash(0),
  mMinMarkNumber(0),
  mMarkNumber(0),
  mDependsNewer(0),
  mDependsOlder(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::~Entry()
//		Purpose: Destructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry::~Entry()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::Entry(const Entry &)
//		Purpose: Copy constructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry::Entry(const Entry &rToCopy)
:
#ifndef BOX_RELEASE_BUILD
  mInvalidated(false),
#endif
  mName(rToCopy.mName),
  mModificationTime(rToCopy.mModificationTime),
  mBackupTime(rToCopy.mBackupTime),
  mObjectID(rToCopy.mObjectID),
  mSizeInBlocks(rToCopy.mSizeInBlocks),
  mFlags(rToCopy.mFlags),
  mAttributesHash(rToCopy.mAttributesHash),
  mAttributes(rToCopy.mAttributes),
  mMinMarkNumber(rToCopy.mMinMarkNumber),
  mMarkNumber(rToCopy.mMarkNumber),
  mDependsNewer(rToCopy.mDependsNewer),
  mDependsOlder(rToCopy.mDependsOlder)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::Entry(const BackupStoreFilename &, int64_t, int64_t, int16_t)
//		Purpose: Constructor from values
//		Created: 2003/08/27
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry::Entry(const BackupStoreFilename &rName, box_time_t ModificationTime, box_time_t BackupTime, int64_t ObjectID, int64_t SizeInBlocks, int16_t Flags, uint64_t AttributesHash)
:
#ifndef BOX_RELEASE_BUILD
  mInvalidated(false),
#endif
  mName(rName),
  mModificationTime(ModificationTime),
  mBackupTime(BackupTime),
  mObjectID(ObjectID),
  mSizeInBlocks(SizeInBlocks),
  mFlags(Flags),
  mAttributesHash(AttributesHash),
  mMinMarkNumber(0),
  mMarkNumber(0),
  mDependsNewer(0),
  mDependsOlder(0)
{
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::TryReading(IOStream &, int)
//		Purpose: Read an entry from a stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::Entry::ReadFromStream(IOStream &rStream, int Timeout, uint32_t magicValue)
{
	ASSERT(!mInvalidated); // Compiled out of release builds
	// Grab the raw bytes from the stream which compose the header
	en_StreamFormat entry;
	if(!rStream.ReadFullBuffer(&entry, sizeof(entry),
		0 /* not interested in bytes read if this fails */, Timeout))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}

	// Do reading first before modifying the variables, to be more exception safe

	// Get the filename
	BackupStoreFilename name;
	name.ReadFromStream(rStream, Timeout);

	// Get the attributes
	mAttributes.ReadFromStream(rStream, Timeout);

	if( magicValue == OBJECTMAGIC_DIR_MAGIC_VALUE_V0 ) {
		mBackupTime = 0;
	} else {
		// Get the Backup Time
		uint64_t backupTime =0;
		if(!rStream.ReadFullBuffer(&backupTime, sizeof(backupTime), 0, Timeout))
		{
			THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
		}
		mBackupTime = box_ntoh64(backupTime);
	}

	// Store the rest of the bits
	mModificationTime =		box_ntoh64(entry.mModificationTime);

	mObjectID = 			box_ntoh64(entry.mObjectID);
	mSizeInBlocks = 		box_ntoh64(entry.mSizeInBlocks);
	mAttributesHash =		box_ntoh64(entry.mAttributesHash);
	mFlags = 				ntohs(entry.mFlags);
	mName =					name;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::WriteToStream(IOStream &)
//		Purpose: Writes the entry to a stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::Entry::WriteToStream(IOStream &rStream, bool IgnoreBackupTime /* this is for compatibilty with older clients */) const
{
	ASSERT(!mInvalidated); // Compiled out of release builds
	// Build a structure
	en_StreamFormat entry;
	entry.mModificationTime = 	box_hton64(mModificationTime);
	entry.mObjectID = 			box_hton64(mObjectID);
	entry.mSizeInBlocks = 		box_hton64(mSizeInBlocks);
	entry.mAttributesHash =		box_hton64(mAttributesHash);
	entry.mFlags = 				htons(mFlags);

	// Write it
	rStream.Write(&entry, sizeof(entry));

	// Write the filename
	mName.WriteToStream(rStream);

	// Write any attributes
	mAttributes.WriteToStream(rStream);
std::cout << "BackupStoreDirectory::Entry::WriteToStream " << IgnoreBackupTime << std::endl;
	if( !IgnoreBackupTime ) {
		std::cout << "writing backup time" << std::endl;
		// Write the backup time
		box_time_t backupTime = box_hton64(mBackupTime);
		rStream.Write((void*)&backupTime, sizeof(mBackupTime));
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::ReadFromStreamDependencyInfo(IOStream &, int)
//		Purpose: Read the optional dependency info from a stream
//		Created: 13/7/04
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::Entry::ReadFromStreamDependencyInfo(IOStream &rStream, int Timeout)
{
	ASSERT(!mInvalidated); // Compiled out of release builds
	// Grab the raw bytes from the stream which compose the header
	en_StreamFormatDepends depends;
	if(!rStream.ReadFullBuffer(&depends, sizeof(depends), 0 /* not interested in bytes read if this fails */, Timeout))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}

	// Store the data
	mDependsNewer = box_ntoh64(depends.mDependsNewer);
	mDependsOlder = box_ntoh64(depends.mDependsOlder);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::WriteToStreamDependencyInfo(IOStream &)
//		Purpose: Write the optional dependency info to a stream
//		Created: 13/7/04
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::Entry::WriteToStreamDependencyInfo(IOStream &rStream) const
{
	ASSERT(!mInvalidated); // Compiled out of release builds
	// Build structure
	en_StreamFormatDepends depends;
	depends.mDependsNewer = box_hton64(mDependsNewer);
	depends.mDependsOlder = box_hton64(mDependsOlder);
	// Write
	rStream.Write(&depends, sizeof(depends));
}
