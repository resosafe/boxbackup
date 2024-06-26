// --------------------------------------------------------------------------
//
// File
//		Name:    HousekeepStoreAccount.h
//		Purpose: Action class to perform housekeeping on a store account
//		Created: 11/12/03
//
// --------------------------------------------------------------------------

#ifndef HOUSEKEEPSTOREACCOUNT__H
#define HOUSEKEEPSTOREACCOUNT__H

#include <string>
#include <set>
#include <vector>

#include "BackupStoreInfo.h"
#include "BackupStoreContext.h"
#include "BackupStoreRefCountDatabase.h"

class BackupStoreDirectory;

class HousekeepingCallback
{
	public:
	virtual ~HousekeepingCallback() {}
	virtual bool CheckForInterProcessMsg(int AccountNum = 0, int MaximumWaitTime = 0) = 0;
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    HousekeepStoreAccount
//		Purpose: Action class to perform housekeeping on a store account
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
class HousekeepStoreAccount
{
public:

	typedef enum {
		DefaultAction=0x0,
		RemoveDeleted=0x1,
		RemoveOldVersions=0x2,
		DisableAutoClean=0x4,
		FixForSnapshotMode=0x8,
		ForceDeleteEmptyDirectories=0x10,
		Force=0x20,
	}ActionFlags;

	HousekeepStoreAccount(int AccountID, const std::string &rStoreRoot,
		int StoreDiscSet, HousekeepingCallback* pHousekeepingCallback);
	~HousekeepStoreAccount();
	
	bool DoHousekeeping(int32_t flags=DefaultAction, bool KeepTryingForever = false, bool lock = true);
	int GetErrorCount() { return mErrorCount; }
	SessionInfos& GetNewSessionsInfos() { return mNewSessionsInfos; }
	
private:
	// utility functions
	void MakeObjectFilename(int64_t ObjectID, std::string &rFilenameOut);

	bool ScanDirectory(int32_t flags, box_time_t SnapshotTime, int64_t ObjectID, BackupStoreInfo& rBackupStoreInfo);
	bool DeleteFiles(BackupStoreInfo& rBackupStoreInfo);
	bool DeleteEmptyDirectories(BackupStoreInfo& rBackupStoreInfo, bool ForceDelete = false);
	void DeleteEmptyDirectory(int64_t dirId, std::vector<int64_t>& rToExamine,
		BackupStoreInfo& rBackupStoreInfo, bool ForceDelete = false);
	BackupStoreRefCountDatabase::refcount_t DeleteFile(int64_t InDirectory,
		int64_t ObjectID,
		BackupStoreDirectory &rDirectory,
		const std::string &rDirectoryFilename,
		BackupStoreInfo& rBackupStoreInfo,
		bool ForceDelete = false);
	void UpdateDirectorySize(BackupStoreDirectory &rDirectory,
		IOStream::pos_type new_size_in_blocks);

	typedef struct
	{
		int64_t mObjectID;
		int64_t mInDirectory;
		int64_t mSizeInBlocks;
		int32_t mMarkNumber;
		int32_t mVersionAgeWithinMark;	// 0 == current, 1 latest old version, etc
		bool    mIsFlagDeleted; // false for files flagged "Old"
	} DelEn;
	
	struct DelEnCompare
	{
		bool operator()(const DelEn &x, const DelEn &y) const;
	};
	
	int mAccountID;
	std::string mStoreRoot;
	int mStoreDiscSet;
	HousekeepingCallback* mpHousekeepingCallback;
	
	int64_t mDeletionSizeTarget;
	
	std::set<DelEn, DelEnCompare> mPotentialDeletions;
	int64_t mPotentialDeletionsTotalSize;
	int64_t mMaxSizeInPotentialDeletions;
	
	// List of directories which are empty, and might be good for deleting
	std::vector<int64_t> mEmptyDirectories;

	// Count of errors found and fixed
	int64_t mErrorCount;
	
	// The re-calculated blocks used stats
	int64_t mBlocksUsed;
	int64_t mBlocksInOldFiles;
	int64_t mBlocksInDeletedFiles;
	int64_t mBlocksInDirectories;

	// Deltas from deletion
	int64_t mBlocksUsedDelta;
	int64_t mBlocksInOldFilesDelta;
	int64_t mBlocksInDeletedFilesDelta;
	int64_t mBlocksInDirectoriesDelta;
	
	// Deletion count
	int64_t mFilesDeleted;
	int64_t mEmptyDirectoriesDeleted;

	// Store informations about the changes made from FixForSnapshotMode
	SessionInfos mNewSessionsInfos; 


	// New reference count list
	std::auto_ptr<BackupStoreRefCountDatabase> mapNewRefs;
	
	// Poll frequency
	int mCountUntilNextInterprocessMsgCheck;

	Logging::Tagger mTagWithClientID;
};

#endif // HOUSEKEEPSTOREACCOUNT__H

