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
#include "BoxTimeToText.h"
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
//		Purpose: Keep the Nth latest records (remove older ones)
//		Created: 2024/03/12
//
// --------------------------------------------------------------------------
void BackupsList::Shift(int MaxCount)
{
    // Validate input
    if(MaxCount < 0) {
        THROW_EXCEPTION(BackupStoreException, Internal)
    }

    if(mList.size() <= static_cast<size_t>(MaxCount)) {
        // Nothing to remove
        return;
    }

    // Calculate how many to remove
    size_t numToRemove = mList.size() - MaxCount;
    
    // Remove oldest records (at the beginning of the set)
    auto it = mList.begin();
    std::advance(it, numToRemove);
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
        // WARNING: Using const_cast on std::set elements is dangerous!
        // std::set elements are const to preserve ordering invariants.
        // However, we need to modify stats without changing the sort key (StartTime).
        // This is safe as long as we only modify non-key fields.
        return const_cast<SessionInfos*>(&(*it));
    } else {
        // If not found, insert a new SessionInfos object and return a pointer to it
        auto insertResult = mList.insert(tempInfo);
        if (!insertResult.second) {
            // Insert failed (shouldn't happen, but be defensive)
            return nullptr;
        }
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
    if(!mList.empty()) {
        return const_cast<SessionInfos*>(&(*mList.begin()));
    }
    return nullptr; 
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::GetLast
//		Purpose: Get the last (most recent) record
//		Created: 2024/03/12
//
// --------------------------------------------------------------------------
SessionInfos* BackupsList::GetLast() 
{
    // get the last (most recent record)
    if(!mList.empty()) {
        return const_cast<SessionInfos*>(&(*mList.rbegin()));
    }
    return nullptr;
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
    
    // Check if stream has data to read
    if(rStream.BytesLeftToRead() == 0) {
        // Empty stream, nothing to read
        return;
    }
    
    // Read the file magic
    uint32_t magic;
    if(rStream.Read(&magic, sizeof(magic), Timeout) != sizeof(magic))
    {
        THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
    }
    
    magic = ntohl(magic);
    if(magic != BACKUPLIST_FILE_MAGIC_V1)
    {
        THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
    }
    
    // Read all session records
    while(rStream.BytesLeftToRead() > 0) {
        // Read the item magic value
        if(rStream.Read(&magic, sizeof(magic), Timeout) != sizeof(magic))
        {
            // Unexpected end of stream
            THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
        }
        
        magic = ntohl(magic);
        if (magic != BACKUPLIST_MAGIC_VALUE_V1)
        {
            THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
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

    if(rootDir.empty()) {
        THROW_EXCEPTION(BackupStoreException, Internal)
    }

    std::string fn = GetFilePath(rootDir);

    // Use RAII for automatic file closing
    {
        FileStream stream(fn.c_str(), O_BINARY | O_RDWR | O_CREAT | O_TRUNC);
        WriteToStream(stream);
        stream.Flush();
        // stream.Close() will be called automatically by destructor
    }
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::WriteToStream
//		Purpose: Write a BackupsList to a stream
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
void BackupsList::WriteToStream(IOStream &rStream, int Timeout)
{ 
    // Seek to beginning to overwrite any existing content
    rStream.Seek(0, IOStream::SeekType_Absolute);

    // Write file magic
    uint32_t fileMagic = htonl(BACKUPLIST_FILE_MAGIC_V1);
    rStream.Write(&fileMagic, sizeof(fileMagic), Timeout);

    // Write each session record
    for(auto it = mList.begin(); it != mList.end(); ++it) {
        // Write record magic
        uint32_t recordMagic = htonl(BACKUPLIST_MAGIC_VALUE_V1);
        rStream.Write(&recordMagic, sizeof(recordMagic), Timeout);
        
        // Write session data
        // Note: const_cast is safe here as we're only serializing data
        const_cast<SessionInfos*>(&(*it))->WriteToStream(rStream, Timeout);
    }
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::OpenStream
//		Purpose: Open or create the backups list file stream
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
std::auto_ptr<IOStream> BackupsList::OpenStream(const std::string &rRootDir) 
{
    std::string fn = GetFilePath(rRootDir);
    std::auto_ptr<IOStream> stream(new FileStream(fn.c_str(), O_BINARY | O_RDWR | O_CREAT));

    // Check if file is empty (newly created)
    IOStream::pos_type bytesAvailable = 0;
    try {
        bytesAvailable = stream->BytesLeftToRead();
    }
    catch(...) {
        // If BytesLeftToRead() throws, treat as empty file
        bytesAvailable = 0;
    }

    if(bytesAvailable == 0) {
        // New file, write the format version magic
        uint32_t magic = htonl(BACKUPLIST_FILE_MAGIC_V1);
        stream->Write(&magic, sizeof(magic));
        // Seek back to beginning for reading
        stream->Seek(0, IOStream::SeekType_Absolute);
    }

    return stream;
}

void BackupsList::Dump()
{
    std::cout << "BackupsList Dump: " << std::endl;
    for (const auto& session : mList) {
        std::cout << "  Start Time: " << BoxTimeToISO8601String(session.GetStartTime(), false)
                  << ", End Time: " << BoxTimeToISO8601String(session.GetEndTime(), false)
                  << ", Added Files: " << session.GetAddedFilesCount()
                  << ", Deleted Files: " << session.GetDeletedFilesCount()
                  << ", Added Directories: " << session.GetAddedDirectoriesCount()
                  << ", Deleted Directories: " << session.GetDeletedDirectoriesCount()
                  << std::endl;
    }
}