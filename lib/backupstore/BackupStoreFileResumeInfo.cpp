#include "BackupStoreFileResumeInfo.h"
#include "FileStream.h"

void BackupStoreResumeFileInfo::Set(BackupStoreResumeInfos &infos) {

    FileStream resume(mFilePath.c_str(), O_BINARY | O_RDWR | O_CREAT);

    int64_t val = infos.GetAttributesHash();
    resume.Write(&val, sizeof(val));
    resume.Write(infos.GetFilePath().c_str(), infos.GetFilePath().size());
    resume.Close();
}
	

BackupStoreResumeInfos BackupStoreResumeFileInfo::Get() {
    FileStream resume(mFilePath.c_str());

    int64_t attrs;
    resume.Read(&attrs, sizeof(attrs));

    std::string filePath;	
    char buf[256];
    int nread;
    while ((nread = resume.Read(buf, sizeof(buf))) > 0) {
        filePath.append(buf, nread);
    }

    return BackupStoreResumeInfos(filePath, attrs);
    
}
