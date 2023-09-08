// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreResumeInfo.h
//		Purpose: Resume file for the backup transfert
//		Created: 2023/09/07
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTORERESUME__H
#define BACKUPSTORERESUME__H

#include <string>

class Protocol;
class IOStream;




// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreResumeInfo
//		Purpose: Resume for the backup transfert
//		Created: 2023/09/07
//
// --------------------------------------------------------------------------
class BackupStoreResumeInfo
{
private:

	std::string mFilePath;
	std::string mResumeFilePath;
    int64_t mResumeFileAttributesHash;

public:
	BackupStoreResumeInfo(int storeDiscSet);
	
	void Set(const std::string &rFilePath, int64_t attributesHash);

	const std::string& GetFilePath() const
	{
		return mFilePath;
	}

	
	int64_t GetAttributesHash() const
	{
		return mAttributesHash;
	}

	
};


#endif // BACKUPSTORERESUME__H

