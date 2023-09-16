// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreResumeInfo.h
//		Purpose: Resume file for the backup transfert
//		Created: 2023/09/07
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTORERESUMEFILEINFO__H
#define BACKUPSTORERESUMEFILEINFO__H

#include <string>
#include "BoxPlatform.h"
#include "BackupStoreContext.h"
#include "IOStream.h"


class BackupStoreResumeInfos {
	private:
		std::string mFilePath;
		int64_t mAttributesHash;

	public:

		BackupStoreResumeInfos(BackupStoreResumeInfos &infos) {
			mFilePath = infos.GetFilePath();
			mAttributesHash = infos.GetAttributesHash();
		}

		BackupStoreResumeInfos(std::string filePath, int64_t attributesHash) {
			mFilePath = filePath;
			mAttributesHash = attributesHash;
		}

		void SetFilePath(std::string filePath) {
			mFilePath = filePath;
		}

		std::string GetFilePath() {
			return mFilePath;
		}

		void SetAttributesHash(int64_t attributesHash) {
			mAttributesHash = attributesHash;
		}

		int64_t GetAttributesHash() {
			return mAttributesHash;
		}

} ;


// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreResumeInfo
//		Purpose: Resume for the backup transfert
//		Created: 2023/09/07
//
// --------------------------------------------------------------------------
class BackupStoreResumeFileInfo
{
	private:
		std::string mFilePath;
		BackupStoreResumeInfos *mInfos;

	public:
		BackupStoreResumeFileInfo(std::string filePath) {
			mFilePath = filePath +"/resume.info";
			mInfos = NULL;
		}
		~BackupStoreResumeFileInfo() {
			if (mInfos != NULL) {
				delete mInfos;
			}
		}

		void Set(BackupStoreResumeInfos &infos);
		BackupStoreResumeInfos* Get();
		int64_t GetFileToBeResumedSize(BackupStoreContext *Context, int64_t AttributesHash);
		std::string GetFilePath();

};


#endif // BACKUPSTORERESUMEFILEINFO__H

