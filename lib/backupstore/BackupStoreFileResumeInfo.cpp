#include "BackupStoreFileResumeInfo.h"
#include "RaidFileController.h"


BackupStoreResumeInfo::BackupStoreResumeInfo(int storeDiscSet) {
    mFilePath = RaidFileController::DiscSetPathToFileSystemPath(storeDiscSet, "resume.info", 1);
}



void BackupStoreResumeInfo::Set(const std::string &rFilePath, int64_t attributesHash) {
    mResumeFilePath = rFilePath;
    mResumeFileAttributesHash = attributesHash;

	FileStream resume(mFilePath.c_str(), O_RDWR | O_CREAT | O_EXCL);
    resume.Write(&mResumeFileAttributesHash, sizeof(mResumeFileAttributesHash));
    resume.Write(mResumeFilePath.c_str(), mResumeFilePath.size());
    resume.Close();
}