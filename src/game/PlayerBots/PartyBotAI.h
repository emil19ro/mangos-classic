#pragma once
#include "CombatBotBaseAI.h"
#include "PlayerBotAI.h"

class PartyBotAI final : public CombatBotBaseAI
{
public:
	PartyBotAI(Player* pLeader, Player* pClone, const CombatBotRoles role, const uint8 class_, const uint8 race, const uint8 gender, const uint32 level, const uint32 mapId, const uint32 instanceId, const float x, const float y, const float z, const float o)
		: CombatBotBaseAI(), m_leaderGUID(pLeader->GetObjectGuid()), m_race(race), m_class(class_), m_level(level), m_gender(gender), m_mapId(mapId), m_instanceId(instanceId), m_x(x), m_y(y), m_z(z), m_o(o)
	{
		m_role = role;
		if (pLeader->GetSession()->GetSecurity() < SEC_ADMINISTRATOR)
			pLeader->IsHiringCompanion = true;
		m_cloneGuid = pClone ? pClone->GetObjectGuid() : ObjectGuid();
		if (pClone) IsClone = true;
		m_updateTimer.Reset(100);
	}
	bool OnSessionLoaded(const PlayerBotEntry* entry, WorldSession* sess) override
	{
		return SpawnNewPlayer(sess, m_class, m_race, m_gender, m_mapId, m_instanceId, m_x, m_y, m_z, m_o, sObjectAccessor.FindPlayer(m_cloneGuid), m_leaderGUID);
	}

	// Constructor Variables
	ObjectGuid m_leaderGUID;
	ObjectGuid m_cloneGuid;
	ShortTimeTracker m_updateTimer;
	uint8 m_race = 0;
	uint8 m_class = 0;
	uint32 m_level = 0;
	uint8 m_gender = static_cast<uint8>(urand(0, 1));
	uint32 m_mapId = 0;
	uint32 m_instanceId = 0;
	bool IsClone = false;
	float m_x = 0.0f;
	float m_y = 0.0f;
	float m_z = 0.0f;
	float m_o = 0.0f;
	// ~Constructor Variables

	// Commands Variables
	bool IsStaying = false; // for .z stay
	bool IsPassive = false; // for .z stop
	bool IsComeToggle = false; // for .z cometoggle
	// ~Commands Variables

	// Movement Sync Variables
	bool MSYNC_CHECK = false;
	float MSYNC_X = 0.0f;
	float MSYNC_Y = 0.0f;
	float MSYNC_Z = 0.0f;
	float MSYNC_O = 0.0f;
	ShortTimeTracker MSYNC_Timer{ 1 * IN_MILLISECONDS };
	// ~Movement Sync Variables

	// OnWhisper Variables
	SpellEntry const* OnWhisper_CastSpell = nullptr;
	ObjectGuid OnWhisper_CastSpellTargetGUID;
	Player* OnWhisper_CastSpellRequester = nullptr;
	Player* m_follow = nullptr;
	float m_set_drink_threshold = 100.0f;
	float m_set_eat_threshold = 100.0f;
	bool m_has_created_portal = false;
	bool m_allow_aoe = true;
	bool m_toggle_come = false;
	Targeting m_targeting_type = TARGETING_FOCUSED;
	bool m_come_location = false;
	bool m_allow_totems = true;
	bool m_DND = false;
	bool ignore_aoe_checks = false;
	bool m_leader_has_fallen = false;
	ShortTimeTracker AOE_Command_Timer{ 0 };
	float m_come_location_x = 0.0f;
	float m_come_location_y = 0.0f;
	float m_come_location_z = 0.0f;
	// ~OnWhisper Variables

	// General Variables
	uint8 IsTier = 0; // for bot gear tier
	bool m_leader_has_flown = false;
	bool m_threat_limiter = true;
	float m_threat_threshold = 90.0f;
	ShortTimeTracker Threat_Emote_Timer{ 0 };
	ShortTimeTracker Local_Emote_Timer{ 0 };
	ShortTimeTracker Gear_Swapping_Timer{ 0 };
	// General Variables

	[[nodiscard]] Player* GetPartyLeader() const;
	void OnPlayerLogin() override;
	void AddToLeaderGroup() const;
	void SetFormationPosition(Player* pLeader);
	bool OverThreat(Unit* pVictim, const float ThreatPercent);
	Unit* SelectAttackerToSilence(SpellEntry const* pSpellEntry);
	void LeaveCombatDruidForm() const;
	void UpdateOutOfCombat_Druid();
	void UpdateOutOfCombat_Hunter();
	void UpdateOutOfCombat_Mage();
	void UpdateOutOfCombat_Paladin();
	void UpdateOutOfCombat_Priest();
	void UpdateOutOfCombat_Rogue();
	void UpdateOutOfCombat_Shaman();
	void UpdateOutOfCombat_Warlock();
	float MyRage() const;
	ShapeshiftForm MyStance() const;
	float RagePoolAmount(const std::string& SpellName) const;
	bool TacticalMastery() const;
	bool DumpRage(Unit* pVictim);
	void UpdateOutOfCombat_Warrior();
	void UpdateInCombat_Druid();
	void UpdateInCombat_Hunter();
	void UpdateInCombat_Mage();
	void UpdateInCombat_Paladin();
	void UpdateInCombat_Priest();
	void UpdateInCombat_Rogue();
	void UpdateInCombat_Shaman();
	void UpdateInCombat_Warlock();
	void UpdateInCombat_Warrior();
	void UpdateOutOfCombatAI();
	void UpdateInCombatAI();
	bool UseHealingPotion() const;
	void MovementSync(const uint32 diff);
	Player* SelectResurrectionTarget() const;
	bool ClearSettingsWhenDead();
	bool InitializeBot();
	bool UseItem(Item* pItem, Unit* pTarget) const;
	Item* GetItemInInventory(uint32 ItemEntry) const;
	bool Follow(Player* pTarget) const;
	bool Teleport(const Player* pTarget) const;
	static InstanceType IsInInstanceType(uint32 ZoneID, uint32 AreaID);
	static uint8 GetCountVoiceTypes(const std::string& VoiceType, uint8 Race, uint8 Gender);
	bool IsFocusMarked(Unit* pVictim, Player* pPlayer) const;
	void Emote(CompanionEmotes Emote, Player* pTarget);
	uint32 TradeHealthstone() const;
	uint32 TradeConjuredFood() const;
	uint32 TradeConjuredWater() const;
	uint32 LevelAppropriateFood() const;
	uint32 LevelAppropriateDrink() const;
	LeaderMountType GetLeaderMountType(const Player* pLeader) const;
	void MountUp(const Player* pLeader) const;
	void MountLogic(const Player* pLeader);
	bool DrinkAndEat(const Player* pLeader) const;
	bool IsBoss(Unit* pVictim);
	Unit* GetMarkedTarget(RaidTargetIcon mark) const;
	bool UseRacials(Unit* pVictim);
	bool AttackStart(Unit* pVictim);
	Unit* GetFocusedVictim(const Player* pLeader);
	bool IsBlacklisted(const std::string& SpellName) const;
	Unit* SelectClosestAttacker() const;
	Unit* SelectLowestHealthAttacker(bool CheckDeadzone = false) const;
	Unit* SelectPartyAttackTarget() const;
	Unit* SelectHighestHealthAttacker(bool CheckDeadzone = false) const;
	Unit* SelectAttackTarget(const Player* pLeader) const;
	void TargetAcquisition(const Player* pLeader);
	static bool FindPathToTarget(const Unit* pUnit, const Unit* pTarget);
	bool OnWhisperComeToggle(Player*& pLeader);
	bool OnWhisperCome(Player*& pLeader);
	bool OnWhisperLogic(Player*& pLeader);
	void UpdateAI(uint32 diff) override;

	std::set<uint32> BossIDs = {
		// Training Dummy - level 60
		90024,
		// Training Dummy - level 63
		90023,
		// Ragefire Chasm (13-18)
		11520, // Taragaman the Hungerer
		11517, // Oggleflint
		11518, // Jergosh the Invoker
		11519, // Bazzalan
		// Wailing Caverns (15-25)
		3671, // Lady Anacondra
		3669, // Lord Cobrahn
		3653, // Kresh
		3670, // Lord Pythas
		3674, // Skum
		5912, // Deviate Faerie Dragon
		3673, // Lord Serpentis
		5775, // Verdan the Everliving
		3654, // Mutanus the Devourer
		// The Deadmines (18-23)
		644,  // Rhahk'Zor
		3586, // Miner Johnson
		643,  // Sneed
		1763, // Gilnid
		646,  // Mr. Smite
		645,  // Cookie
		647,  // Captain Greenskin
		639,  // Edwin VanCleef
		// Shadowfang Keep (22-30)
		3886, // Razorclaw the Butcher
		3887, // Baron Silverlaine
		4278, // Commander Springvale
		4279, // Odo the Blindwatcher
		3872, // Deathsworn Captain
		4274, // Fenrus the Devourer
		3927, // Wolf Master Nandos
		4275, // Archmage Arugal
		// The Stockade (22-30)
		1720, // Bruegal Ironknuckle
		1696, // Targorr the Dread
		1666, // Kam Deepfury
		1717, // Hamhock
		1663, // Dextren Ward
		1716, // Bazil Thredd
		// Blackfathom Deeps (24-32)
		4887, // Ghamoo-ra
		4831, // Lady Sarevess
		12902, // Lorgus Jett
		6243, // Gelihast
		12876, // Baron Aquanis
		4830, // Old Serra'kis
		4832, // Twilight Lord Kelris
		4829, // Aku'mai
		// Gnomeregan (29-38)
		7079, // Viscous Fallout
		7361, // Grubbis
		6235, // Electrocutioner 6000
		6229, // Crowd Pummeler 9-60
		6228, // Dark Iron Ambassador
		7800, // Mekgineer Thermaplugg
		// Razorfen Kraul (30-40)
		6168, // Roogug
		4428, // Death Speaker Jargba
		4420, // Overlord Ramtusk
		4842, // Earthcaller Halmgar
		4424, // Aggem Thorncurse
		4422, // Agathelos the Raging
		4425, // Blind Hunter
		4421, // Charlga Razorflank
		// Scarlet Monastery (26-45)
		3983, // Interrogator Vishas
		6490, // Azshir the Sleepless
		6488, // Fallen Champion
		6489, // Ironspine
		4543, // Bloodmage Thalnos
		3974, // Houndmaster Loksey
		6487, // Arcanist Doan
		3975, // Herod
		4542, // High Inquisitor Fairbanks
		3976, // Scarlet Commander Mograine
		3977, // High Inquisitor Whitemane
		// Razorfen Downs (40-50)
		7355, // Tuten'kash
		7356, // Plaguemaw the Rotting
		7357, // Mordresh Fire Eye
		8567, // Glutton
		7354, // Ragglesnout
		7358, // Amnennar the Coldbringer
		// Uldaman (42-52)
		6910, // Revelosh
		6907, // Eric
		7228, // Ironaya
		7023, // Obsidian Sentinel
		7206, // Ancient Stone Keeper
		7291, // Galgann Firehammer
		4854, // Grimlok
		2748, // Archaedas
		// Zul'Farrak (44-54)
		10080, // Sandarr Dunereaver
		7272, // Theka the Martyr
		8127, // Antu'sul
		7271, // Witch Doctor Zum'rah
		7274, // Sandfury Executioner
		7796, // Nekrum Gutchewer
		7275, // Shadowpriest Sezz'ziz
		10082, // Zerillis
		10081, // Dustwraith
		7604, // Sergeant Bly
		7267, // Chief Ukorz Sandscalp
		7797, // Ruuzlu
		7795, // Hydromancer Velratha
		7273, // Gahz'rilla
		// Maraudon (46-55)
		13601, // Tinkerer Gizlock
		12236, // Lord Vyletongue
		13282, // Noxxion
		12258, // Razorlash
		12237, // Meshlok the Harvester
		12225, // Celebras the Cursed
		12203, // Landslide
		13596, // Rotgrip
		12201, // Princess Theradras
		// Temple of Atal'Hakkar (50-60)
		8580, // Atal'alarion
		5716, // Zul'Lor
		5713, // Gasher
		5714, // Loro
		5712, // Zolo
		5717, // Mijan
		5715, // Hukku
		5710, // Jammal'an the Prophet
		5711, // Ogom the Wretched
		5720, // Weaver
		5721, // Dreamscythe
		8443, // Avatar of Hakkar
		5722, // Hazzas
		5719, // Morphaz
		5709, // Shade of Eranikus
		// Blackrock Depths (52-60)
		9018, // High Interrogator Gerstahn
		9319, // Houndmaster Grebmar
		9025, // Lord Roccor
		10096, // High Justice Grimstone
		9017, // Lord Incendius
		9041, // Warder Stilgiss
		9042, // Verek
		9476, // Watchman Doomgrip
		9056, // Fineous Darkvire
		9016, // Bael'Gar
		9033, // General Angerforge
		8983, // Golem Lord Argelmach
		9537, // Hurley Blackbreath
		9502, // Phalanx
		9499, // Plugger Spazzring
		9543, // Ribbly Screwspigot
		9156, // Ambassador Flamelash
		8923, // Panzor the Invincible
		9938, // Magmus
		8929, // Princess Moira Bronzebeard
		9019, // Emperor Dagran Thaurissan
		// Lower Blackrock Spire (55-60)
		10263, // Burning Felguard
		9196, // Highlord Omokk
		9218, // Spirestone Battle Lord
		9217, // Spirestone Lord Magus
		9236, // Shadow Hunter Vosh'gajin
		9237, // War Master Voone
		9596, // Bannok Grimaxe
		10596, // Mother Smolderweb
		10376, // Crystal Fang
		10584, // Urok Doomhowl
		9736, // Quartermaster Zigris
		10220, // Halycon
		10268, // Gizrul the Slavener
		9718, // Ghok Bashguud
		9219, // Spirestone Butcher
		9568, // Overlord Wyrmthalak
		// Upper Blackrock Spire (55-60)
		9816, // Pyroguard Emberseer
		10264, // Solakar Flamewreath
		10899, // Goraluk Anvilcrack
		10509, // Jed Runewatcher
		10339, // Gyth
		10429, // Warchief Rend Blackhand
		10430, // The Beast
		10363, // General Drakkisath
		// Dire Maul (58-60)
		14354, // Pusillin
		11490, // Zevrim Thornhoof
		13280, // Hydrospawn
		14327, // Lethtendris
		11492, // Alzzin the Wildshaper
		11489, // Tendris Warpwood
		11467, // Tsu'zee
		11488, // Illyanna Ravenoak
		11487, // Magister Kalendris
		11496, // Immol'thar
		14506, // Lord Hel'nurath
		11486, // Prince Tortheldrin
		14326, // Guard Mol'dar
		14322, // Stomper Kreeg
		14321, // Guard Fengus
		14323, // Guard Slip'kik
		14325, // Captain Kromcrush
		14324, // Cho'Rush the Observer
		11501, // King Gordok
		// Scholomance (58-60)
		14861, // Blood Steward of Kirtonos
		10506, // Kirtonos the Herald
		10503, // Jandice Barov
		11622, // Rattlegore
		14516, // Death Knight Darkreaver
		10433, // Marduk Blackpool
		10432, // Vectus
		10508, // Ras Frostwhisper
		11261, // Doctor Theolen Krastinov
		10901, // Lorekeeper Polkelt
		10505, // Instructor Malicia
		10502, // Lady Illucia Barov
		10507, // The Ravenian
		1853,  // Darkmaster Gandling
		// Stratholme (58-60)
		11082, // Stratholme Courier
		10558, // Hearthsinger Forresten
		10393, // Skul
		11143, // Postmaster Malown
		10516, // The Unforgiven
		10808, // Timmy the Cruel
		10811, // Archivist Galford
		11032, // Malor the Zealous
		10997, // Cannon Master Willey
		10812, // Grand Crusader Dathrohan
		10813, // Balnazzar
		10809, // Stonespine
		10437, // Nerub'enkan
		10438, // Maleki the Pallid
		10436, // Baroness Anastari
		10435, // Magistrate Barthilas
		10439, // Ramstein the Gorger
		10440  // Baron Rivendare
	};
};
