#include "Box.h"

#include "BackupStoreFileResumeInfo.h"
#include "BackupStoreException.h"
#include "autogen_BackupStoreException.h"
#include "RaidFileController.h"

#include "FileStream.h"

void BackupStoreResumeFileInfo::Set(BackupStoreResumeInfos *infos)
{
    mInfos = infos;
  
    // and write it to disk
    FileStream resume(mFilePath.c_str(), O_BINARY | O_RDWR | O_CREAT | O_TRUNC);
    int64_t oid = infos->GetObjectID();
    resume.Write(&oid, sizeof(oid));
    int64_t attrs = infos->GetAttributesHash();
    resume.Write(&attrs, sizeof(attrs));
    resume.Write(infos->GetFilePath().c_str(), infos->GetFilePath().size());
    resume.Close();
}
	

BackupStoreResumeInfos* BackupStoreResumeFileInfo::Get()
{

   if(mInfos == NULL)
   {
        FileStream resume(mFilePath.c_str());

        int64_t oid ;
        resume.Read(&oid, sizeof(oid));

        int64_t attrs;
        resume.Read(&attrs, sizeof(attrs));

        std::string filePath;	
        char buf[256];
        int nread;
        while ((nread = resume.Read(buf, sizeof(buf))) > 0)
        {
            filePath.append(buf, nread);
        }

        mInfos = new BackupStoreResumeInfos(filePath, oid, attrs);
    }

    return mInfos;
}

int64_t BackupStoreResumeFileInfo::GetFileToBeResumedSize(BackupStoreContext *Context, int64_t ObjectID, int64_t AttributesHash)
{
    BackupStoreResumeInfos* infos = Get();

    if(ObjectID == infos->GetObjectID() && AttributesHash == infos->GetAttributesHash())
    {
        // infos match, check if file exists
        EMU_STRUCT_STAT st;
        if(EMU_LSTAT(infos->GetFilePath().c_str(), &st) == 0)
        {
            // file exists, we may be able to resume
            // Get store info from context
            const BackupStoreInfo &rinfo(Context->GetBackupStoreInfo());

            // Find block size
            RaidFileController &rcontroller(RaidFileController::GetController());
            RaidFileDiscSet &rdiscSet(rcontroller.GetDiscSet(rinfo.GetDiscSetNumber()));
            return st.st_size;
        } 
        else
        {
            THROW_EXCEPTION(BackupStoreException, CannotResumeUpload);
        }
    } 
    else
    {
        THROW_EXCEPTION(BackupStoreException, CannotResumeUpload);
    }
    
}

void BackupStoreResumeFileInfo::Delete()
{
    EMU_UNLINK(mFilePath.c_str());
}

std::string BackupStoreResumeFileInfo::GetFilePath()
{
    BackupStoreResumeInfos* infos = Get();
    return infos->GetFilePath();
}