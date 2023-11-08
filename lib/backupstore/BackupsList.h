// --------------------------------------------------------------------------
//
// File
//		Name:    BackupsList.h
//		Purpose: Backups List with basics infos
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------

#ifndef BACKUPSLIST__H
#define BACKUPSLIST__H

#include <memory>
#include <string>
#include <vector>

#include "BackupStoreException.h"
#include "CollectInBufferStream.h"
#include "BoxTime.h"
#include "BackupStoreContext.h"

#define BACKUPLIST_FILE_MAGIC_V1	0x424c5631 // BLV1
#define BACKUPLIST_MAGIC_VALUE_V1	0x425631 // BV1

#define BACKUPS_FILENAME	"backups.lst"

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupsList
//		Purpose: store information about backups
//		Created: 2023/10/30
//
// --------------------------------------------------------------------------
class BackupsList
{
	friend class BackupStoreCheck;
public:
	~BackupsList();
private:
	// No copying allowed
	BackupsList(const BackupsList &);

public:
	// Creation through static functions only
	BackupsList();
	BackupsList(IOStream &stream);
	BackupsList(const std::string &rRootDir);

	void RemoveAt(box_time_t Time);
	void ReadFromStream(IOStream &rStream, int Timeout = IOStream::TimeOutInfinite);
	void WriteToStream(IOStream &rStream, int Timeout = IOStream::TimeOutInfinite);
	void Save();

	static std::auto_ptr<IOStream> OpenStream(const std::string &rRootDir);
	
	static std::string GetFilePath(const std::string &rRootDir)
	{
		if(rRootDir.empty() )
		{
			THROW_EXCEPTION(BackupStoreException, Internal)
		}
		
		std::string fn(rRootDir + BACKUPS_FILENAME);
		return fn;
	}

	void AddRecord(SessionInfos &rInfos);
	static void AddRecord(const std::string &rRootDir, SessionInfos &rInfos);
	

	// Get the number of backups
	int GetCount() const
	{
		return mList.size();
	}

	std::vector<SessionInfos> &GetList()
	{
		return mList;
	}


private:
	std::vector<SessionInfos> mList;
	std::string mRootDir;
};

#endif // BACKUPSLIST__H


