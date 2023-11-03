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
	


	std::string fn =  GetFilePath(rRootDir);

    FileStream stream(fn.c_str(), O_BINARY | O_RDWR | O_CREAT);
    stream.Seek(0, IOStream::SeekType_End);
    
    // store the format version
    uint32_t magic = htonl(BACKUPLIST_MAGIC_VALUE_V1);
    stream.Write(&magic, sizeof(magic));
    rInfos.WriteToStream(stream);
	
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupsList::Load
//		Purpose: Load a BackupsList from a stream
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
void BackupsList::ReadFromStream(IOStream &rStream, int Timeout)
{

    mList.clear();

    while(rStream.BytesLeftToRead()) {
    // Read the magic value
        uint32_t magic;
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
//		Name:    BackupsList::Add
//		Purpose: Add a new backup to the list
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
std::auto_ptr<IOStream> BackupsList::OpenStream(const std::string &rRootDir) 
{

    std::string fn = GetFilePath(rRootDir);

    return std::auto_ptr<IOStream>(new FileStream(fn.c_str(), O_BINARY | O_RDWR));
	
}