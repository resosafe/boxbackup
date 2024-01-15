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

int64_t BackupStoreResumeFileInfo::GetFileToBeResumedSize(BackupStoreContext *Context, int64_t AttributesHash)
{
    BackupStoreResumeInfos* infos = Get();

    if(AttributesHash == infos->GetAttributesHash())
    {
        // infos match, check if file exists
        EMU_STRUCT_STAT st;
        if(EMU_LSTAT(infos->GetFilePath().c_str(), &st) == 0)
        {
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




void BackupStoreResumeFileInfo::Cleanup()
{
    try
    {        
        EMU_UNLINK(Get()->GetFilePath().c_str());
        Delete();
    }
    catch (...)
    {
        // ignore
    }
}


void BackupStoreResumeFileInfo::Delete()
{
    delete mInfos;
    mInfos = NULL;
    int r = EMU_UNLINK(mFilePath.c_str());
    if(r != 0)
    {
        printf("%m\n");
    }
}