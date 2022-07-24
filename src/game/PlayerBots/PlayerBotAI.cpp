#include "PlayerBotAI.h"
#include "PlayerBotMgr.h"
#include "Social/SocialMgr.h"

bool PlayerBotAI::OnSessionLoaded(const PlayerBotEntry* entry, WorldSession* sess)
{
	//sess->LoginPlayerBot(static_cast<ObjectGuid>(entry->GetGUID())); // TODO CHECK IF WORKS
	return true;
}

void PlayerBotAI::GenerateCompanionName(std::string& Name, const ObjectGuid LeaderGUID, Races Race, Gender Gender) const
{
	Player* OriginalLeader = ObjectAccessor::FindPlayerNotInWorld(LeaderGUID);
	if (!OriginalLeader) return;
	const Group* pGroup = OriginalLeader->GetGroup();

	// If in raid group try to generate names based on leader's Name
	if (pGroup && pGroup->IsRaidGroup())
	{
		// Create the first part of the Bot's name based on the Leader's name
		const auto lname = OriginalLeader->GetName();
		uint8 nchar = 0;
		if (strlen(lname) >= 4)
			nchar = 4;
		else if (strlen(lname) >= 3)
			nchar = 3;
		else if (strlen(lname) >= 2)
			nchar = 2;
		char LeaderName[5] = {};
		strncpy_s(LeaderName, OriginalLeader->GetName(), nchar);
		const std::string NameFirstPart = LeaderName;

		// Pick a random name suffix from the map provided
		std::vector<std::string> suffix_vector = sPlayerBotMgr.GetNameSuffixes();
		std::shuffle(suffix_vector.begin(), suffix_vector.end(), std::mt19937(std::random_device()()));
		for (const auto& NameSecondPart : suffix_vector)
		{
			std::string FusedName = NameFirstPart + NameSecondPart;
			const PlayerBotEntry* pBot = sPlayerBotMgr.GetBotWithName(FusedName);
			if (!pBot && !sObjectMgr.GetPlayerGuidByName(FusedName))
			{
				Name = FusedName;
				break;
			}
		}

		// If the provided list of suffixes wasn't enough then use another one that's based on pet names
		if (!Name.length())
		{
			std::vector<std::string> generated_suffixes = sObjectMgr.GenerateNameSuffixes();
			std::shuffle(generated_suffixes.begin(), generated_suffixes.end(), std::mt19937(std::random_device()()));
			for (const auto& NameSecondPart : generated_suffixes)
			{
				std::string Fusedname = NameFirstPart + NameSecondPart;
				const PlayerBotEntry* pBot = sPlayerBotMgr.GetBotWithName(Fusedname);
				if (!pBot && !sObjectMgr.GetPlayerGuidByName(Fusedname))
				{
					Name = Fusedname;
					break;
				}
			}
		}
	}
	// If not in raid group then pick a fantasy Name
	else
	{
		// Use a given 2000+ fantasy names map
		auto Names = sPlayerBotMgr.GetNameMap();
		auto NamesVector = Names[std::make_pair(Race, Gender)];
		std::shuffle(NamesVector.begin(), NamesVector.end(), std::mt19937(std::random_device()()));
		for (const auto& itr : NamesVector)
		{
			const PlayerBotEntry* pBot = sPlayerBotMgr.GetBotWithName(itr);
			if (!pBot && !sObjectMgr.GetPlayerGuidByName(itr))
			{
				Name = itr;
				break;
			}
		}

		// If the provided list of names wasn't enough then use succubus names
		if (!Name.length())
		{
			do
			{
				auto itr = sObjectMgr.GeneratePetName(1863);
				normalizePlayerName(itr);
				const PlayerBotEntry* pBot = sPlayerBotMgr.GetBotWithName(itr);
				if (!pBot)
					break;
			} while (true);
		}
	}
}

bool PlayerBotAI::SpawnNewPlayer(WorldSession* sess, const uint8 _class, const uint8 _race, const uint8 gender, const uint32 mapId, const uint32 instanceId, const float x, const float y, const float z, const float o, Player* pClone, ObjectGuid m_leaderGuid) const
{
	MANGOS_ASSERT(botEntry);  // NOLINT
	// Find a unique accountName
	std::string name;

	GenerateCompanionName(name, m_leaderGuid, static_cast<Races>(_race), static_cast<Gender>(gender));

	botEntry->SetName(name);
	const uint8 genderG = pClone ? pClone->GetByteValue(UNIT_FIELD_BYTES_0, UNIT_BYTES_0_OFFSET_GENDER) : gender;

	// Leo - appearance done right
	uint8 skin = 0;
	uint8 face = 0;
	uint8 hairStyle = 0;
	uint8 hairColor = 0;
	uint8 facialHair = 0;
	if (pClone)
	{
		skin = pClone->GetByteValue(PLAYER_BYTES, 0);
		face = pClone->GetByteValue(PLAYER_BYTES, 1);
		hairStyle = pClone->GetByteValue(PLAYER_BYTES, 2);
		hairColor = pClone->GetByteValue(PLAYER_BYTES, 3);
		facialHair = pClone->GetByteValue(PLAYER_BYTES_2, 0);
	}
	else
	{
		if (_race == RACE_HUMAN)
		{
			if (gender == GENDER_MALE)
			{
				face = static_cast<UINT8>(urand(0, 11));
				hairColor = static_cast<UINT8>(urand(0, 9));
				skin = static_cast<uint8>(urand(0, 9));
				hairStyle = static_cast<UINT8>(urand(0, 11));
				facialHair = static_cast<UINT8>(urand(0, 8));
			}
			else
			{
				face = static_cast<UINT8>(urand(0, 14));
				hairColor = static_cast<UINT8>(urand(0, 9));
				skin = static_cast<uint8>(urand(0, 9));
				hairStyle = static_cast<UINT8>(urand(0, 18));
				facialHair = static_cast<UINT8>(urand(0, 6));
			}
		}
		else if (_race == RACE_ORC)
		{
			if (gender == GENDER_MALE)
			{
				face = static_cast<UINT8>(urand(0, 8));
				hairColor = static_cast<UINT8>(urand(0, 7));
				skin = static_cast<uint8>(urand(0, 8));
				hairStyle = static_cast<UINT8>(urand(0, 6));
				facialHair = static_cast<UINT8>(urand(0, 10));
			}
			else
			{
				face = static_cast<UINT8>(urand(0, 8));
				hairColor = static_cast<UINT8>(urand(0, 7));
				skin = static_cast<uint8>(urand(0, 8));
				hairStyle = static_cast<UINT8>(urand(0, 7));
				facialHair = static_cast<UINT8>(urand(0, 6));
			}
		}
		else if (_race == RACE_DWARF)
		{
			if (gender == GENDER_MALE)
			{
				face = static_cast<UINT8>(urand(0, 9));
				hairColor = static_cast<UINT8>(urand(0, 9));
				skin = static_cast<uint8>(urand(0, 8));
				hairStyle = static_cast<UINT8>(urand(0, 10));
				facialHair = static_cast<UINT8>(urand(0, 10));
			}
			else
			{
				face = static_cast<UINT8>(urand(0, 9));
				hairColor = static_cast<UINT8>(urand(0, 9));
				skin = static_cast<uint8>(urand(0, 8));
				hairStyle = static_cast<UINT8>(urand(0, 13));
				facialHair = static_cast<UINT8>(urand(0, 5));
			}
		}
		else if (_race == RACE_NIGHTELF)
		{
			if (gender == GENDER_MALE)
			{
				face = static_cast<UINT8>(urand(0, 8));
				hairColor = static_cast<UINT8>(urand(0, 7));
				skin = static_cast<uint8>(urand(0, 8));
				hairStyle = static_cast<UINT8>(urand(0, 6));
				facialHair = static_cast<UINT8>(urand(0, 5));
			}
			else
			{
				face = static_cast<UINT8>(urand(0, 8));
				hairColor = static_cast<UINT8>(urand(0, 7));
				skin = static_cast<uint8>(urand(0, 8));
				hairStyle = static_cast<UINT8>(urand(0, 6));
				facialHair = static_cast<UINT8>(urand(0, 9));
			}
		}
		else if (_race == RACE_UNDEAD)
		{
			if (gender == GENDER_MALE)
			{
				face = static_cast<UINT8>(urand(0, 9));
				hairColor = static_cast<UINT8>(urand(0, 9));
				skin = static_cast<uint8>(urand(0, 5));
				hairStyle = static_cast<UINT8>(urand(0, 19));
				facialHair = static_cast<UINT8>(urand(0, 16));
			}
			else
			{
				face = static_cast<UINT8>(urand(0, 9));
				hairColor = static_cast<UINT8>(urand(0, 9));
				skin = static_cast<uint8>(urand(0, 5));
				hairStyle = static_cast<UINT8>(urand(0, 17));
				facialHair = static_cast<UINT8>(urand(0, 7));
			}
		}
		else if (_race == RACE_TAUREN)
		{
			if (gender == GENDER_MALE)
			{
				face = static_cast<UINT8>(urand(0, 4));
				hairColor = static_cast<UINT8>(urand(0, 2));
				skin = static_cast<uint8>(urand(0, 18));
				hairStyle = static_cast<UINT8>(urand(0, 7));
				facialHair = static_cast<UINT8>(urand(0, 6));
			}
			else
			{
				face = static_cast<UINT8>(urand(0, 3));
				hairColor = static_cast<UINT8>(urand(0, 2));
				skin = static_cast<uint8>(urand(0, 10));
				hairStyle = static_cast<UINT8>(urand(0, 6));
				facialHair = static_cast<UINT8>(urand(0, 4));
			}
		}
		else if (_race == RACE_GNOME)
		{
			if (gender == GENDER_MALE)
			{
				face = static_cast<UINT8>(urand(0, 6));
				hairColor = static_cast<UINT8>(urand(0, 8));
				skin = static_cast<uint8>(urand(0, 4));
				hairStyle = static_cast<UINT8>(urand(0, 6));
				facialHair = static_cast<UINT8>(urand(0, 7));
			}
			else
			{
				face = static_cast<UINT8>(urand(0, 6));
				hairColor = static_cast<UINT8>(urand(0, 8));
				skin = static_cast<uint8>(urand(0, 4));
				hairStyle = static_cast<UINT8>(urand(0, 6));
				facialHair = static_cast<UINT8>(urand(0, 6));
			}
		}
		else if (_race == RACE_TROLL)
		{
			if (gender == GENDER_MALE)
			{
				face = static_cast<UINT8>(urand(0, 4));
				hairColor = static_cast<UINT8>(urand(0, 9));
				skin = static_cast<uint8>(urand(0, 5));
				hairStyle = static_cast<UINT8>(urand(0, 5));
				facialHair = static_cast<UINT8>(urand(0, 10));
			}
			else
			{
				face = static_cast<UINT8>(urand(0, 5));
				hairColor = static_cast<UINT8>(urand(0, 9));
				skin = static_cast<uint8>(urand(0, 5));
				hairStyle = static_cast<UINT8>(urand(0, 4));
				facialHair = static_cast<UINT8>(urand(0, 5));
			}
		}
	}

	auto newChar = new Player(sess);
	if (const auto guid = static_cast<uint32>(botEntry->GetGUID()); !newChar->Create(guid, name, _race, _class, genderG, skin, face, hairStyle, hairColor, facialHair, 0))
	{
		sLog.outError("PlayerBotAI::SpawnNewPlayer: Unable to create a player!");
		delete newChar;
		return false;
	}

	BigNumber N;
	sess->InitializeAnticheat(N);
	newChar->SetLocationMapId(mapId);
	newChar->SetLocationInstanceId(instanceId);
	newChar->SetHomebindToLocation(WorldLocation(1, 16224.961914f, 16257.753906f, 13.159891f, 4.544266f), 876); // GM Island
	newChar->GetMotionMaster()->Initialize();
	newChar->setCinematic(2);

	// Set instance
	if (instanceId && mapId > 1) // Not a continent
	{
		const auto state = dynamic_cast<DungeonPersistentState*>(sMapPersistentStateMgr.AddPersistentState(sMapStore.LookupEntry(mapId), instanceId, time(nullptr) + 3600, false, true));
		newChar->BindToInstance(state, true, true);
	}
	// Generate position
	Map* map = sMapMgr.FindMap(mapId, instanceId);
	if (!map)
	{
		sLog.outError("PlayerBotAI::SpawnNewPlayer: Map (%u, %u) not found!", mapId, instanceId);
		delete newChar;
		return false;
	}
	newChar->Relocate(x, y, z, o);
	newChar->SetMap(map);
	newChar->SaveRecallPosition();
	if (!newChar->GetMap()->Add(newChar))
	{
		sLog.outError("PlayerBotAI::SpawnNewPlayer: Unable to add player to map!");
		delete newChar;
		return false;
	}
	sess->SetPlayer(newChar, newChar->GetGUIDLow());
	sObjectAccessor.AddObject(newChar);
	newChar->SetCanModifyStats(true);
	newChar->UpdateAllStats();
	newChar->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);

	return true;
}