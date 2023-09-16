#include "Box.h"

#include "BackupStoreFileResumeInfo.h"
#include "BackupStoreException.h"
#include "autogen_BackupStoreException.h"

#include "FileStream.h"

void BackupStoreResumeFileInfo::Set(BackupStoreResumeInfos &infos) {

    if(mInfos == NULL) {
        mInfos = new BackupStoreResumeInfos(infos);
    } else {
        *mInfos = infos;
    }

    FileStream resume(mFilePath.c_str(), O_BINARY | O_RDWR | O_CREAT);

    int64_t val = infos.GetAttributesHash();
    resume.Write(&val, sizeof(val));
    resume.Write(infos.GetFilePath().c_str(), infos.GetFilePath().size());
    resume.Close();
}
	

BackupStoreResumeInfos* BackupStoreResumeFileInfo::Get() {

   if(mInfos == NULL)
   {
        FileStream resume(mFilePath.c_str());

        int64_t attrs;
        resume.Read(&attrs, sizeof(attrs));

        std::string filePath;	
        char buf[256];
        int nread;
        while ((nread = resume.Read(buf, sizeof(buf))) > 0) {
            filePath.append(buf, nread);
        }

        mInfos = new BackupStoreResumeInfos(filePath, attrs);
    }

    return mInfos;
}

#include <iostream>
#include "RaidFileController.h"
int64_t BackupStoreResumeFileInfo::GetFileToBeResumedSize(BackupStoreContext *Context, int64_t AttributesHash) {
    BackupStoreResumeInfos* infos = Get();

    if (AttributesHash == infos->GetAttributesHash()) {

        EMU_STRUCT_STAT st;
        if (EMU_LSTAT(infos->GetFilePath().c_str(), &st) == 0) {
            // file exists, we may be able to resume
            // Get store info from context
            const BackupStoreInfo &rinfo(Context->GetBackupStoreInfo());

            // Find block size
            RaidFileController &rcontroller(RaidFileController::GetController());
            RaidFileDiscSet &rdiscSet(rcontroller.GetDiscSet(rinfo.GetDiscSetNumber()));

	
            return st.st_size;
        } else {
            THROW_EXCEPTION(BackupStoreException, BackupStoreException::OSFileError);
        }
    } else {
        THROW_EXCEPTION(BackupStoreException, AttributesNotUnderstood);
    }
    
}

std::string BackupStoreResumeFileInfo::GetFilePath() {
    BackupStoreResumeInfos* infos = Get();
    return infos->GetFilePath();
}