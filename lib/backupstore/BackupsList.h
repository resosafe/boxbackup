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

#define BACKUPLIST_MAGIC_VALUE_V1	0x424c5631 // V1



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

	static std::auto_ptr<BackupsList> Load(int32_t AccountID, const std::string &rRootDir, int DiscSet, bool ReadOnly, int64_t *pRevisionID = 0);
	void ReadFromStream(IOStream &rStream, int Timeout = IOStream::TimeOutInfinite);
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
};

#endif // BACKUPSLIST__H


