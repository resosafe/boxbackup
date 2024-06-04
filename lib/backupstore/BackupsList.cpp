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
//		Name:    BackupsList::AddRecord
//		Purpose: Add a new backup to the list
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
void BackupsList::AddRecord(SessionInfos &rInfos) 
{
    mList.insert(rInfos);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::Shift
//		Purpose: Keep the Nth latest records
//		Created: 2024/03/12
//
// --------------------------------------------------------------------------
void BackupsList::Shift(int MaxCount)
{
    auto it = mList.begin();
    std::advance(it, mList.size() - MaxCount);
    mList.erase(mList.begin(), it);
}
    

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::Get
//		Purpose: Get a session infos at the specified start time
//		Created: 2024/03/12
//
// --------------------------------------------------------------------------
SessionInfos* BackupsList::Get(box_time_t StartTime) 
{
    SessionInfos tempInfo;
    tempInfo.SetStart(StartTime);

    // Try to find the SessionInfos object in the set
    auto it = mList.find(tempInfo);

    if (it != mList.end()) {
        // If found, return a pointer to the existing object
        return const_cast<SessionInfos*>(&(*it));
    } else {
        // If not found, insert a new SessionInfos object and return a pointer to it
        std::pair<std::set<SessionInfos>::iterator, bool> insertResult = mList.insert(tempInfo);
        return const_cast<SessionInfos*>(&(*insertResult.first));
    }
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::GetFirst
//		Purpose: Get the first (oldest) record
//		Created: 2024/03/12
//
// --------------------------------------------------------------------------
SessionInfos* BackupsList::GetFirst() 
{
    // get the first (oldest record)  
    if(mList.size() > 0) {
        return const_cast<SessionInfos*>(&(*mList.begin()));
    }
    return NULL; 
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
        mList.insert(infos);
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
void BackupsList::Save(const std::string &rRootDir) {

    std::string rootDir = rRootDir;
    if(rootDir.empty()) {
        rootDir = mRootDir;
    }

   if(rootDir.empty() ) {
        THROW_EXCEPTION(BackupStoreException, Internal)
    }

    std::string fn = GetFilePath(rootDir);
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
    rStream.Seek(0, IOStream::SeekType_Absolute);

    uint32_t magic = htonl(BACKUPLIST_FILE_MAGIC_V1);
    rStream.Write(&magic, sizeof(magic));

    for(auto it = mList.begin(); it != mList.end(); ++it) {
        uint32_t magic = htonl(BACKUPLIST_MAGIC_VALUE_V1);
        rStream.Write(&magic, sizeof(magic), Timeout);
        (const_cast<SessionInfos*>(&(*it)))->WriteToStream(rStream, Timeout);
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