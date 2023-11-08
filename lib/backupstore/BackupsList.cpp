// --------------------------------------------------------------------------
//
// File
//		Name:    BackupsList.cpp
//		Purpose: Backups history list
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "BackupsList.h"
#include "RaidFileWrite.h"
#include "RaidFileRead.h"
#include "Archive.h"

#include "MemLeakFindOn.h"



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::BackupsList()
//		Purpose: Default constructor
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
BackupsList::BackupsList()
{ 
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::BackupsList()
//		Purpose: Constructorand init from a stream
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
BackupsList::BackupsList(IOStream &stream)
{
    ReadFromStream(stream);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::BackupsList()
//		Purpose: Constructorand init from a stream
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
BackupsList::BackupsList(const std::string &rRootDir):
mRootDir(rRootDir)
{
    // file will be created by default
    std::auto_ptr<IOStream> stream = OpenStream(rRootDir);
    ReadFromStream(*stream);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::~BackupsList
//		Purpose: Destructor
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
BackupsList::~BackupsList()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::Add
//		Purpose: Add a new backup to the list
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
void BackupsList::AddRecord(const std::string &rRootDir, SessionInfos &rInfos) 
{
    std::auto_ptr<IOStream> stream = OpenStream(rRootDir);
    stream->Seek(0, IOStream::SeekType_End);
    
    // store the format version
    uint32_t magic = htonl(BACKUPLIST_MAGIC_VALUE_V1);
    stream->Write(&magic, sizeof(magic));
    rInfos.WriteToStream(*stream);
	
}


void BackupsList::AddRecord(SessionInfos &rInfos) 
{
    mList.push_back(rInfos);

    if(mRootDir.empty()) {
        THROW_EXCEPTION(BackupStoreException, Internal)
    }
    std::auto_ptr<IOStream> stream = OpenStream(mRootDir);
    stream->Seek(0, IOStream::SeekType_End);
    
    // store the format version
    uint32_t magic = htonl(BACKUPLIST_MAGIC_VALUE_V1);
    stream->Write(&magic, sizeof(magic));
    rInfos.WriteToStream(*stream);
	
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::RemoveAt
//		Purpose: Remove all records at or before the given time
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
void BackupsList::RemoveAt(box_time_t Time)
{
    for(std::vector<SessionInfos>::iterator it = mList.begin(); it != mList.end(); ++it) {
        if(it->GetStartTime() <= Time) {
            mList.erase(it);
        }
    }
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::ReadFromStream
//		Purpose: Load a BackupsList from a stream
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
void BackupsList::ReadFromStream(IOStream &rStream, int Timeout)
{
    mList.clear();
    // Read the file magic
    uint32_t magic;
    rStream.Read(&magic, sizeof(magic), Timeout);
    magic = ntohl(magic);
    while(rStream.BytesLeftToRead()) {
        // Read the item magic value
        rStream.Read(&magic, sizeof(magic), Timeout);
        magic = ntohl(magic);
        if (magic != BACKUPLIST_MAGIC_VALUE_V1)
        {
            THROW_EXCEPTION(BackupStoreException, Internal)
        }

        SessionInfos infos;
        infos.ReadFromStream(rStream, Timeout);
        mList.push_back(infos);
    }
}




// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::Save
//		Purpose: Save a BackupsList to a file
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
void BackupsList::Save() {

   if(mRootDir.empty()) {
        THROW_EXCEPTION(BackupStoreException, Internal)
    }
    std::string fn = GetFilePath(mRootDir);
    FileStream stream(fn.c_str(), O_BINARY | O_RDWR | O_CREAT);
    WriteToStream(stream);

}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::WriteFromStream
//		Purpose: Write a BackupsList to a stream
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
void BackupsList::WriteToStream(IOStream &rStream, int Timeout)
{
    for(std::vector<SessionInfos>::iterator it = mList.begin(); it != mList.end(); ++it) {
        uint32_t magic = htonl(BACKUPLIST_MAGIC_VALUE_V1);
        rStream.Write(&magic, sizeof(magic), Timeout);
        it->WriteToStream(rStream, Timeout);
    }
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::Add
//		Purpose: Add a new backup to the list
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
std::auto_ptr<IOStream> BackupsList::OpenStream(const std::string &rRootDir) 
{

    std::string fn = GetFilePath(rRootDir);
    std::auto_ptr<IOStream> stream(new FileStream(fn.c_str(), O_BINARY | O_RDWR | O_CREAT));

    if( !stream->BytesLeftToRead() ) {
        // store the format version
        uint32_t magic = htonl(BACKUPLIST_FILE_MAGIC_V1);
        stream->Write(&magic, sizeof(magic));
        stream->Seek(0, IOStream::SeekType_Absolute);
    }

    return stream;
	
}