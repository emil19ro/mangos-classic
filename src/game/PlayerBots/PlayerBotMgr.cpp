#include "CombatBotBaseAI.h"
#include "PlayerBotMgr.h"
#include "PlayerBotAI.h"
#include "PartyBotAI.h"
#include "Accounts/AccountMgr.h"
#include "World/World.h"

INSTANTIATE_SINGLETON_1(PlayerBotMgr);

/*********************************************************/
/***                    BOT MANAGEMENT                 ***/
/*********************************************************/

void PlayerBotMgr::DeleteAll()
{
	m_stats.onlineCount = 0;
	m_stats.loadingCount = 0;
	for (auto& [first, second] : m_bots)
	{
		if (second->GetState() != BOT_STATE_OFFLINE)
		{
			second->SetState(BOT_STATE_OFFLINE);
			second->SetRequestRemoval(true);
		}
	}
}

void PlayerBotMgr::Load()
{
	// Clean
	DeleteAll();
	m_bots.clear();

	// Load usable account ID
	QueryResult* result = LoginDatabase.PQuery(
		"SELECT MAX(`id`)"
		" FROM `account`");
	if (!result)
	{
		sLog.outError("Playerbot: unable to load max account id.");
		return;
	}
	const Field* fields = result->Fetch();
	m_maxAccountId = fields[0].GetUInt32() + 10000;
	delete result;

	// Loaders
	LoadTalentMap();
	LoadSpellTrainers();
	LoadSpellMap();
	LoadNameMap();
}

void PlayerBotMgr::SetBotOnline(PlayerBotEntry* e)
{
	e->SetState(BOT_STATE_ONLINE);
	m_stats.totalBots++;
}

void PlayerBotMgr::SetBotOffline(PlayerBotEntry* e)
{
	e->SetState(BOT_STATE_OFFLINE);
	m_stats.totalBots--;
}

void PlayerBotMgr::UnregisterBot(const std::map<uint64, PlayerBotEntry*>::iterator& iter)
{
	if (iter->second->GetState() == BOT_STATE_LOADING)
		m_stats.loadingCount--;
	else if (iter->second->GetState() == BOT_STATE_ONLINE)
		m_stats.onlineCount--;
	if (iter->second->GetRequestRemoval())
		iter->second->SetRequestRemoval(false);
	SetBotOffline(iter->second);
}

PlayerBotEntry* PlayerBotMgr::GetBotWithName(const std::string& name) const
{
	for (const auto& [BotGUID, BotEntry] : m_bots)
		if (BotEntry->GetName() == name)
			return BotEntry;

	return nullptr;
}

bool PlayerBotMgr::AddBot(PlayerBotAI* ai)
{
	const uint32 accountID = GenBotAccountId();
	const auto e = new PlayerBotEntry(sObjectMgr.GeneratePlayerLowGuid(), accountID);
	e->SetAI(ai);
	e->SetAccountName("[BOT " + std::to_string(m_stats.totalBots + 1) + "]");
	ai->SetBotEntry(e);
	m_bots[e->GetGUID()] = e;
	e->SetState(BOT_STATE_LOADING);
	const auto session = new WorldSession(accountID, nullptr, SEC_PLAYER, EXPANSION_TBC, 0, LOCALE_enUS, e->GetAccountName(), ACCOUNT_FLAG_SHOW_ANTICHEAT, 0, false);
	session->SetBot(e);
	sWorld.AddSession(session);
	m_stats.loadingCount++;
	return true;
}

void PlayerBotMgr::OnPlayerInWorld(Player* pPlayer)
{
	if (const PlayerBotEntry* BotEntry = pPlayer->GetSession()->GetBot())
	{
		pPlayer->SetAI(BotEntry->GetAI()); // Sets i_AI
		BotEntry->GetAI()->SetPlayer(pPlayer); // Create "me"
		BotEntry->GetAI()->OnPlayerLogin(); // Make bot not attackable until initialized
	}
}

void PlayerBotMgr::Update(const uint32 diff)
{
	// Manager update interval
	Manager_Update_Timer.Update(diff);
	if (Manager_Update_Timer.Passed())
		Manager_Update_Timer.Reset(2 * IN_MILLISECONDS);
	else return;

	// Remove despawned Bots from m_bots evidence
	if (!m_bots.empty())
	{
		auto iter = m_bots.begin();
		while (iter != m_bots.end())
		{
			if (iter->second->GetRequestRemoval())
			{
				UnregisterBot(iter);
				if (iter->second->GetAI() && iter->second->GetAI()->me)
					iter->second->GetAI()->me->RemoveFromGroup();
				if (WorldSession* sess = sWorld.FindSession(iter->second->GetAccountID()))
					sess->LogoutPlayer();
				iter = m_bots.erase(iter);
			}
			else ++iter;
		}
	}

	// Login Bots & Send Packets
	if (!m_bots.empty())
	{
		for (auto iter = m_bots.begin(); iter != m_bots.end(); ++iter)
		{
			std::vector<WorldPacket> Mails = iter->second->GetMailBox();
			if (iter->second->GetState() == BOT_STATE_ONLINE && !iter->second->GetMailBox().empty() && iter->second->GetAI() && iter->second->GetAI()->me)
			{
				iter->second->ClearMailBox();
				for (const auto& packet : Mails)
					iter->second->GetAI()->ProcessPacket(packet);
			}

			if (iter->second->GetState() != BOT_STATE_LOADING)
				continue;

			WorldSession* sess = sWorld.FindSession(iter->second->GetAccountID());
			if (!sess)
				continue;

			if (iter->second->GetAI()->OnSessionLoaded(iter->second, sess))
			{
				SetBotOnline(iter->second);
				m_stats.loadingCount--;
			}
			else UnregisterBot(iter);
		}
	}

	// Set Bot update interval
	if (!m_bots.empty() && !BOT_UPDATE_INTERVAL_MANUAL)
	{
		BOT_UPDATE_INTERVAL_TIMER.Update(diff);
		if (BOT_UPDATE_INTERVAL_TIMER.Passed())
		{
			const uint32 count = m_stats.totalBots;
			if (count < 50)
				BOT_UPDATE_INTERVAL = 100;
			else if (count < 100)
				BOT_UPDATE_INTERVAL = 500;
			else
				BOT_UPDATE_INTERVAL = 1000;

			if (BOT_UPDATE_INTERVAL_PREVIOUS != BOT_UPDATE_INTERVAL)
			{
				sLog.outString("----------");
				sLog.outString("Bot update interval: %u", BOT_UPDATE_INTERVAL);
				sLog.outString("Bot count: %u", count);
				sLog.outString("----------");
				BOT_UPDATE_INTERVAL_PREVIOUS = BOT_UPDATE_INTERVAL;
			}
			BOT_UPDATE_INTERVAL_TIMER.Reset(60 * IN_MILLISECONDS);
		}
	}
}

/*********************************************************/
/***                    LOADERS		                   ***/
/*********************************************************/

void PlayerBotMgr::LoadTalentMap()
{
	for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
	{
		TalentEntry const* talentInfo = sTalentStore.LookupEntry(i);
		if (!talentInfo)
			continue;

		TalentTabEntry const* talentTabInfo = sTalentTabStore.LookupEntry(talentInfo->TalentTab);
		if (!talentTabInfo)
			continue;

		const uint32 spellID = talentInfo->RankID[0];
		if (!spellID)
			continue;

		auto const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellID);
		if (!spellInfo)
			continue;

		auto Class = static_cast<Classes>(std::log2(talentTabInfo->ClassMask) + 1);

		TalentMap[Class].emplace(spellInfo->SpellName[0], talentInfo->TalentID);
	}
}

void PlayerBotMgr::LoadSpellTrainers()
{
	SpellTrainers[CLASS_WARRIOR] = 26332;
	SpellTrainers[CLASS_PALADIN] = 26327;
	SpellTrainers[CLASS_HUNTER] = 26325;
	SpellTrainers[CLASS_ROGUE] = 26329;
	SpellTrainers[CLASS_PRIEST] = 26328;
	SpellTrainers[CLASS_SHAMAN] = 26330;
	SpellTrainers[CLASS_MAGE] = 26326;
	SpellTrainers[CLASS_WARLOCK] = 26331;
	SpellTrainers[CLASS_DRUID] = 26324;
}

void PlayerBotMgr::LoadSpellMap()
{
	for (uint32 i = 0; i < sSpellTemplate.GetMaxEntry(); i++)
	{
		auto const* pSpellEntry = sSpellTemplate.LookupEntry<SpellEntry>(i);
		if (!pSpellEntry)
			continue;

		if (pSpellEntry->HasAttribute(SPELL_ATTR_PASSIVE))
			continue;

		if (pSpellEntry->HasAttribute(SPELL_ATTR_DO_NOT_DISPLAY))
			continue;

		if (pSpellEntry->HasAttribute(SPELL_ATTR_IS_TRADESKILL))
			continue;

		if (pSpellEntry->Effect[EFFECT_INDEX_0] == SPELL_EFFECT_TRADE_SKILL)
			continue;

		spellNameEntryMap[pSpellEntry->SpellName[0]].insert(pSpellEntry->Id);
	}
}

void PlayerBotMgr::LoadNameMap()
{
	NamesSuffixes =
	{
		"bro",
		"buddy",
		"cule",
		"culus",
		"el",
		"elle",
		"en",
		"erel",
		"ers",
		"et",
		"etto",
		"ie",
		"il",
		"illa",
		"illi",
		"illo",
		"illus",
		"ina",
		"ine",
		"ing",
		"ini",
		"ino",
		"ish",
		"ita",
		"ito",
		"kin",
		"le",
		"let",
		"lette",
		"ling",
		"mcfly",
		"mini",
		"o",
		"ock",
		"ola",
		"ole",
		"oli",
		"olo",
		"olus",
		"ooga",
		"ot",
		"otte",
		"pal",
		"rel",
		"sfriend",
		"sie",
		"sky",
		"ster",
		"sy",
		"tron",
		"ula",
		"ule",
		"ulum",
		"y",
		"zug"
	};

	NameMap[std::make_pair(RACE_HUMAN, GENDER_MALE)] =
	{
		"Adam",
		"Addi",
		"Aldo",
		"Alvertos",
		"Amos",
		"Andreas",
		"Archerd",
		"Audric",
		"Benediktus",
		"Berit",
		"Birch",
		"Brad",
		"Brodie",
		"Brody",
		"Bromley",
		"Bruce",
		"Burcet",
		"Casimir",
		"Cesar",
		"Clark",
		"Claudius",
		"Cooper",
		"Damon",
		"Ean",
		"Elbert",
		"Elmer",
		"Emilio",
		"Endrik",
		"Erwin",
		"Fitz",
		"Fonteyne",
		"Garrin",
		"Gordon",
		"Greyson",
		"Gunthar",
		"Hacket",
		"Harry",
		"Helmo",
		"Hinz",
		"Howe",
		"Hugo",
		"Jake",
		"Jamir",
		"Jason",
		"Jayden",
		"Jendrick",
		"Jimmy",
		"Kasey",
		"Kasper",
		"Kilby",
		"Killian",
		"Klaudius",
		"Koby",
		"Kole",
		"Lamont",
		"Leal",
		"Lex",
		"Louis",
		"Maik",
		"Mark",
		"Mathias",
		"Menard",
		"Mika",
		"Neuman",
		"Newell",
		"Nilson",
		"Omari",
		"Palmer",
		"Paul",
		"Paule",
		"Raven",
		"Rawlins",
		"Raydon",
		"Reinhold",
		"Richard",
		"Rob",
		"Robby",
		"Ron",
		"Rushkin",
		"Rutherford",
		"Rypley",
		"Sigismund",
		"Smith",
		"Stein",
		"Sven",
		"Sylwester",
		"Tate",
		"Tavin",
		"Tayrese",
		"Teddie",
		"Thorn",
		"Todd",
		"Tom",
		"Tucker",
		"Vittorio",
		"Waldron",
		"Ward",
		"Warden",
		"Webley",
		"Zander",

		"Ooga",
	};

	NameMap[std::make_pair(RACE_HUMAN, GENDER_FEMALE)] =
	{
		"Abbigail",
		"Adalene",
		"Adelia",
		"Alayna",
		"Aletta",
		"Alison",
		"Anabelle",
		"Ancelina",
		"Anissa",
		"Baby",
		"Beatrix",
		"Bertille",
		"Bobbi",
		"Brigitte",
		"Britney",
		"Brunhilda",
		"Calantha",
		"Camile",
		"Carin",
		"Carla",
		"Carlotta",
		"Cassie",
		"Cathrin",
		"Ciri",
		"Cirilla",
		"Cora",
		"Corine",
		"Cortney",
		"Dalia",
		"Darlene",
		"Dominika",
		"Ellen",
		"Emanuela",
		"Enna",
		"Enrica",
		"Eveline",
		"Florence",
		"Ginette",
		"Gloria",
		"Heda",
		"Helmi",
		"Irma",
		"Isolda",
		"Janika",
		"Jazmin",
		"Jeena",
		"Jessica",
		"Jolande",
		"Jolie",
		"Julchen",
		"Justyne",
		"Katelynn",
		"Kimbell",
		"Kylie",
		"Kim",
		"Laurina",
		"Leslie",
		"Liesel",
		"Lucia",
		"Luzie",
		"Lynnette",
		"Mady",
		"Maliyah",
		"Marcelina",
		"Mareen",
		"Marene",
		"Mariette",
		"Marija",
		"Maya",
		"Melina",
		"Mia",
		"Mianna",
		"Mimi",
		"Mistee",
		"Monique",
		"Nada",
		"Nadja",
		"Nancie",
		"Nannette",
		"Nanni",
		"Nele",
		"Ninette",
		"Philomena",
		"Rachael",
		"Rebeca",
		"Riannon",
		"Rosi",
		"Shea",
		"Sheyla",
		"Sidney",
		"Silvia",
		"Stephanie",
		"Tamira",
		"Ursula",
		"Valeri",
		"Wendy",
		"Yasmine",
		"Yedda",
		"Yelena",
		"Zina",
	};

	NameMap[std::make_pair(RACE_DWARF, GENDER_MALE)] =
	{
		"Amdan",
		"Amduhr",
		"Armdor",
		"Armdur",
		"Armgran",
		"Baermyl",
		"Baerryl",
		"Baldar",
		"Balnam",
		"Bandek",
		"Banmin",
		"Banrum",
		"Barkohm",
		"Belgrim",
		"Belthrum",
		"Benmand",
		"Berdrom",
		"Bermund",
		"Bernus",
		"Bhalnam",
		"Bharduhr",
		"Bharrig",
		"Bhelgarn",
		"Bhelgron",
		"Bhelkam",
		"Bramram",
		"Brumkom",
		"Bungrun",
		"Daeradin",
		"Dolren",
		"Ebdek",
		"Ebgrim",
		"Emmin",
		"Ermren",
		"Faradin",
		"Fargrun",
		"Farnir",
		"Gargarn",
		"Garmar",
		"Germand",
		"Gimgran",
		"Gimmar",
		"Graldus",
		"Gralnir",
		"Grammiir",
		"Grangrom",
		"Granthrum",
		"Gremdren",
		"Grilmar",
		"Grygus",
		"Grynir",
		"Gulmek",
		"Harmus",
		"Hjoltharn",
		"Hulman",
		"Hulorm",
		"Hurdar",
		"Hurnir",
		"Karbrek",
		"Karnik",
		"Kharmur",
		"Kramdohr",
		"Kramnir",
		"Magmek",
		"Meldar",
		"Melmek",
		"Melnik",
		"Melrigg",
		"Morkohm",
		"Mornus",
		"Morthrun",
		"Muirnik",
		"Murdohr",
		"Murdrak",
		"Ragmun",
		"Ragnom",
		"Ragthrun",
		"Rangarn",
		"Ranmek",
		"Regram",
		"Tharmek",
		"Thelmar",
		"Thermur",
		"Thertharm",
		"Thogram",
		"Thormund",
		"Thorrig",
		"Thorrom",
		"Thurgron",
		"Thurnum",
		"Thurnur",
		"Tygram",
		"Tynus",
		"Umdohr",
		"Ummek",
		"Urmgarn",
		"Urmkum",
		"Urmmiir",
		"Vonduhr",
		"Vongrun",
	};

	NameMap[std::make_pair(RACE_DWARF, GENDER_FEMALE)] =
	{
		"Baerselle",
		"Barleil",
		"Barvan",
		"Bellen",
		"Belleria",
		"Bellinn",
		"Bonnglian",
		"Braenniss",
		"Braldeth",
		"Bralnan",
		"Brandryn",
		"Branres",
		"Brenlynn",
		"Brenra",
		"Brenres",
		"Bretlinn",
		"Bretnis",
		"Bretvia",
		"Brillela",
		"Brillelynn",
		"Brilleri",
		"Brillesyl",
		"Brilletyn",
		"Brillewin",
		"Brolwynn",
		"Bronri",
		"Bruldyl",
		"Brylwin",
		"Brytnora",
		"Bylledora",
		"Dearnip",
		"Ednip",
		"Elrielle",
		"Gemdeth",
		"Gerres",
		"Gerwyn",
		"Ginidi",
		"Gwanla",
		"Gwenlyl",
		"Gwinwyn",
		"Gwynleil",
		"Gwynmyl",
		"Gwynnyl",
		"Jenglia",
		"Jennvan",
		"Jindelle",
		"Jintyn",
		"Jinwin",
		"Jynlyl",
		"Kaitnis",
		"Kaitri",
		"Kardora",
		"Karlyl",
		"Karsora",
		"Kartin",
		"Ketsyl",
		"Lassthiel",
		"Lesdyl",
		"Lyesryn",
		"Lysnyl",
		"Lysra",
		"Lyssva",
		"Lystin",
		"Maerlyl",
		"Maevdora",
		"Maevselle",
		"Marnan",
		"Marnip",
		"Misros",
		"Myrgwyn",
		"Myrnia",
		"Myrra",
		"Mysnora",
		"Mysria",
		"Mystnys",
		"Naerbera",
		"Nalmyl",
		"Nalnyss",
		"Nasleil",
		"Nesdish",
		"Nesnyl",
		"Nisnis",
		"Raenra",
		"Redria",
		"Rundeth",
		"Runlyn",
		"Runmura",
		"Rynmyla",
		"Sarria",
		"Sarwyn",
		"Soldielle",
		"Tasgwyn",
		"Tasnip",
		"Tizva",
		"Tornura",
		"Tyshdish",
		"Tyshnys",
		"Tyshri",
		"Tyshthel",
		"Tysleil",
	};

	NameMap[std::make_pair(RACE_NIGHTELF, GENDER_MALE)] =
	{
		"Adrieth",
		"Adron",
		"Alegorn",
		"Alydran",
		"Amalaeth",
		"Andaran",
		"Anerian",
		"Anlas",
		"Arin",
		"Barirenthil",
		"Bolminon",
		"Celian",
		"Cerelndaar",
		"Charandiir",
		"Dalnar",
		"Daros",
		"Denadaan",
		"Denvoris",
		"Dradil",
		"Draesin",
		"Emaelath",
		"Enaenas",
		"Endas",
		"Enlian",
		"Fadraeth",
		"Fahnados",
		"Felladan",
		"Filagorn",
		"Filaste",
		"Filealar",
		"Galaelath",
		"Galalias",
		"Galanai",
		"Garannul",
		"Gathidan",
		"Hallad",
		"Iladean",
		"Ilanas",
		"Illaeleath",
		"Illanai",
		"Iyleath",
		"Iyries",
		"Jareath",
		"Kadiir",
		"Kadrannul",
		"Keldel",
		"Kerlad",
		"Lardaan",
		"Larris",
		"Lilalias",
		"Lydras",
		"Malurandiir",
		"Manados",
		"Manlas",
		"Manris",
		"Mardral",
		"Mareas",
		"Mavorus",
		"Meladil",
		"Melidris",
		"Midan",
		"Moraenas",
		"Mydraris",
		"Mylais",
		"Mytaeleath",
		"Nadaar",
		"Naries",
		"Nedras",
		"Nythannul",
		"Radolar",
		"Raethidan",
		"Reldris",
		"Rellaen",
		"Saenis",
		"Sallian",
		"Salorne",
		"Seldant",
		"Selellorn",
		"Selodraen",
		"Sililaeth",
		"Sydras",
		"Sylaleas",
		"Taldon",
		"Teladaron",
		"Terenlar",
		"Terin",
		"Therias",
		"Thydraes",
		"Uydlas",
		"Uydran",
		"Valvoris",
		"Wadlas",
		"Wereath",
		"Yadras",
		"Yalian",
		"Yedent",
		"Yedryn",
		"Yleallaeth",
		"Ylilonian",
		"Ylthaan",
	};

	NameMap[std::make_pair(RACE_NIGHTELF, GENDER_FEMALE)] =
	{
		"Aaylina",
		"Aenysil",
		"Aethalas",
		"Alaeth",
		"Alalara",
		"Alavanna",
		"Alylaea",
		"Alylas",
		"Alynna",
		"Alywen",
		"Anas",
		"Ariadia",
		"Aririen",
		"Astatia",
		"Becavaria",
		"Byalea",
		"Calysia",
		"Canysae",
		"Cayla",
		"Ceri",
		"Dasia",
		"Denriale",
		"Densera",
		"Deri",
		"Doristra",
		"Dulthea",
		"Elirya",
		"Ellalara",
		"Ellalia",
		"Enasea",
		"Enyurea",
		"Esalleas",
		"Felercia",
		"Fylaenea",
		"Fyraei",
		"Fyrerinar",
		"Galaenya",
		"Idrialah",
		"Idrilina",
		"Iyhara",
		"Jaeldris",
		"Javaria",
		"Jerryssia",
		"Kaleae",
		"Kindia",
		"Kylae",
		"Kylalina",
		"Kylysea",
		"Kyrae",
		"Lalae",
		"Lanas",
		"Lelathea",
		"Lilene",
		"Lilenna",
		"Lylalia",
		"Lyreath",
		"Maedri",
		"Maliriia",
		"Malysae",
		"Melia",
		"Melse",
		"Menysa",
		"Menyssea",
		"Merelnaya",
		"Meshia",
		"Myrnae",
		"Mytalaria",
		"Mytaleas",
		"Nalysea",
		"Narnae",
		"Nerae",
		"Nilyssa",
		"Nyraei",
		"Nyridyia",
		"Nytheaith",
		"Relea",
		"Retarii",
		"Rhyasia",
		"Rhylin",
		"Saetaria",
		"Saleath",
		"Samara",
		"Selene",
		"Shernea",
		"Silindea",
		"Sillas",
		"Silnira",
		"Sisera",
		"Sylethis",
		"Veslea",
		"Visdira",
		"Warai",
		"Wenysa",
		"Wiyell",
		"Yalaria",
		"Yelysae",
		"Yerae",
		"Yileath",
		"Ylanna",
		"Ylora",
	};

	NameMap[std::make_pair(RACE_GNOME, GENDER_MALE)] =
	{
		"Adaldin",
		"Alkok",
		"Alonk",
		"Athkeke",
		"Atkotez",
		"Atlic",
		"Bendik",
		"Benus",
		"Ceeldas",
		"Ceelkan",
		"Cireelkenk",
		"Cunizz",
		"Daklish",
		"Dandeern",
		"Deckan",
		"Dimkock",
		"Dinlin",
		"Diri",
		"Dotivik",
		"Dumkezz",
		"Eclish",
		"Eenceck",
		"Eerkick",
		"Ekic",
		"Erkonk",
		"Ethirn",
		"Fibleclizz",
		"Fithkurn",
		"Fitkiz",
		"Fukli",
		"Futloz",
		"Gattlock",
		"Gaweeci",
		"Geecink",
		"Geetkic",
		"Giklak",
		"Giminkack",
		"Girirn",
		"Gitles",
		"Gitlos",
		"Glatkorn",
		"Gleeclish",
		"Glerli",
		"Glinci",
		"Glonceeck",
		"Gnomkick",
		"Gnuris",
		"Gondo",
		"Heedin",
		"Hitlinic",
		"Hodirkic",
		"Huklarn",
		"Ibliduk",
		"Ikis",
		"Ildo",
		"Inlack",
		"Irleti",
		"Irlush",
		"Kadees",
		"Keemolees",
		"Keerkiflen",
		"Kickizz",
		"Kinkirn",
		"Kleenkee",
		"Klenlic",
		"Kluckern",
		"Klunkosh",
		"Kokluldeck",
		"Kreekle",
		"Krelduk",
		"Krencu",
		"Kribiz",
		"Krinkogush",
		"Kumedi",
		"Linleck",
		"Lurish",
		"Lutleck",
		"Meenurn",
		"Mibonk",
		"Milku",
		"Mimloblee",
		"Omkin",
		"Onigo",
		"Pavindon",
		"Peecenk",
		"Piklosen",
		"Pirle",
		"Pitleris",
		"Tabes",
		"Tamun",
		"Tanko",
		"Teecotick",
		"Thamki",
		"Theerku",
		"Tirkaz",
		"Tirli",
		"Tithi",
		"Udurik",
		"Ulkin",
		"Utun",
	};

	NameMap[std::make_pair(RACE_GNOME, GENDER_FEMALE)] =
	{
		"Albi",
		"Amkiflo",
		"Askink",
		"Bemink",
		"Bildis",
		"Bixil",
		"Bodis",
		"Byskill",
		"Dalez",
		"Danela",
		"Debreka",
		"Dillish",
		"Dipeek",
		"Doskick",
		"Dubi",
		"Filki",
		"Giru",
		"Gissa",
		"Gitkis",
		"Glinkli",
		"Glolla",
		"Gnepi",
		"Gnibri",
		"Gnili",
		"Gninni",
		"Gnissi",
		"Gondes",
		"Gupick",
		"Gynni",
		"Hazel",
		"Hifuli",
		"Hislel",
		"Huba",
		"Ibu",
		"Ilatkli",
		"Imill",
		"Imkimo",
		"Indee",
		"Iskosh",
		"Jibiri",
		"Jomin",
		"Joxe",
		"Juleyz",
		"Kebo",
		"Kelka",
		"Kellu",
		"Kilifi",
		"Kixiz",
		"Klilo",
		"Kuky",
		"Kytlen",
		"Lankeell",
		"Larus",
		"Lenkla",
		"Libry",
		"Lidi",
		"Limes",
		"Linzi",
		"Littlish",
		"Lonki",
		"Lysloni",
		"Matha",
		"Minke",
		"Miro",
		"Motko",
		"Muki",
		"Namik",
		"Nellu",
		"Nenni",
		"Nida",
		"Nilbyfick",
		"Ninda",
		"Nopeys",
		"Nyskas",
		"Ota",
		"Peril",
		"Pidible",
		"Pothil",
		"Pynuse",
		"Sessi",
		"Seti",
		"Siblixo",
		"Sidi",
		"Simis",
		"Sinka",
		"Sinkeyll",
		"Subus",
		"Texi",
		"Thakle",
		"Thaxes",
		"Thimo",
		"Thotke",
		"Thubu",
		"Tissill",
		"Tuposi",
		"Tytla",
		"Ukee",
		"Ullun",
		"Umkibe",
		"Yris",
	};

	NameMap[std::make_pair(RACE_DRAENEI, GENDER_MALE)] =
	{
		"Abirok",
		"Actevon",
		"Aerin",
		"Ahemorhan",
		"Ahoan",
		"Ahoravar",
		"Akallius",
		"Aosom",
		"Aotol",
		"Arem",
		"Asgan",
		"Avon",
		"Behodoru",
		"Behoonan",
		"Billon",
		"Bilmen",
		"Bralir",
		"Brared",
		"Bremat",
		"Breonan",
		"Celok",
		"Cemuun",
		"Corleth",
		"Daedaar",
		"Dereluun",
		"Derevuun",
		"Derluun",
		"Drelok",
		"Dryras",
		"Dumos",
		"Dutun",
		"Eocaid",
		"Fallag",
		"Funnan",
		"Gugan",
		"Haalius",
		"Hanluun",
		"Hardrimus",
		"Harmiis",
		"Herrada",
		"Herrok",
		"Hobadiir",
		"Hobakhen",
		"Hobliir",
		"Hunan",
		"Hurdraan",
		"Hurlius",
		"Hurvad",
		"Ihul",
		"Iradai",
		"Ivuun",
		"Izdaan",
		"Jovanos",
		"Jurhun",
		"Jursun",
		"Kaalaen",
		"Kaamus",
		"Kanrelon",
		"Kelel",
		"Khaas",
		"Kilat",
		"Kildiir",
		"Lamanar",
		"Lucel",
		"Lynuus",
		"Mahag",
		"Mahan",
		"Mared",
		"Menerelon",
		"Menlis",
		"Menral",
		"Muared",
		"Muhenos",
		"Nahoanos",
		"Narada",
		"Ninni",
		"Nobuag",
		"Oadum",
		"Ocluun",
		"Onallus",
		"Onmos",
		"Orerok",
		"Orlaen",
		"Osed",
		"Pamanar",
		"Pasuun",
		"Ramravar",
		"Ramsera",
		"Ranar",
		"Rualen",
		"Steluun",
		"Thormen",
		"Toraltun",
		"Torvan",
		"Vasera",
		"Vetol",
		"Voraan",
		"Vordal",
		"Voruul",
		"Zodor",
	};

	NameMap[std::make_pair(RACE_DRAENEI, GENDER_FEMALE)] =
	{
		"Aellestra",
		"Aevin",
		"Aihula",
		"Ailtaa",
		"Aini",
		"Arett",
		"Aveirah",
		"Avmere",
		"Azihula",
		"Azileen",
		"Azirae",
		"Binrin",
		"Bomina",
		"Celeil",
		"Cenaraa",
		"Cohula",
		"Colae",
		"Corix",
		"Corlara",
		"Edida",
		"Egosaana",
		"Ellogin",
		"Elloine",
		"Elny",
		"Enhaa",
		"Enirah",
		"Enraani",
		"Ensera",
		"Esmis",
		"Farama",
		"Gherin",
		"Gorla",
		"Hafun",
		"Halhaa",
		"Hali",
		"Halleen",
		"Halret",
		"Halrin",
		"Hateema",
		"Inela",
		"Inluu",
		"Irbina",
		"Irestra",
		"Irett",
		"Irmena",
		"Irna",
		"Islesia",
		"Jaelela",
		"Jalnii",
		"Jatia",
		"Jolmae",
		"Kazal",
		"Keirioa",
		"Kelret",
		"Kelthaa",
		"Kenura",
		"Kevi",
		"Khazi",
		"Lunaelli",
		"Lunala",
		"Manaa",
		"Mesrah",
		"Miarioa",
		"Migin",
		"Milaa",
		"Mimae",
		"Mohaa",
		"Momhaa",
		"Mually",
		"Multaa",
		"Mumhri",
		"Munei",
		"Mushri",
		"Nanii",
		"Nanohula",
		"Naramis",
		"Nesstraa",
		"Neua",
		"Noraga",
		"Noralaara",
		"Noralesia",
		"Norin",
		"Nuraan",
		"Phati",
		"Remedine",
		"Remtra",
		"Serii",
		"Sestraa",
		"Sial",
		"Sulgin",
		"Thelarua",
		"Ummere",
		"Valnaraa",
		"Valrin",
		"Valustra",
		"Vuulesia",
		"Vuunei",
		"Xaruna",
		"Xiguni",
		"Yarah",
	};

	NameMap[std::make_pair(RACE_ORC, GENDER_MALE)] =
	{
		"Aderm",
		"Akesh",
		"Aklurm",
		"Avenk",
		"Bold",
		"Bosul",
		"Bratur",
		"Brild",
		"Brorgonk",
		"Brost",
		"Bruvduta",
		"Celgim",
		"Cos",
		"Crach",
		"Crahle",
		"Crath",
		"Cresta",
		"Crul",
		"Crurdo",
		"Daldugg",
		"Darok",
		"Dingu",
		"Dontorl",
		"Doth",
		"Drahlgoth",
		"Dramarg",
		"Dreknuth",
		"Drosh",
		"Gagmugne",
		"Garzu",
		"Gimu",
		"Gonim",
		"Grem",
		"Gukk",
		"Hatich",
		"Hekmes",
		"Hokten",
		"Horg",
		"Hoth",
		"Hotzakust",
		"Husugmim",
		"Kivnegast",
		"Kokme",
		"Krahlzukk",
		"Kralost",
		"Krangundal",
		"Krelarn",
		"Kretruro",
		"Krurmu",
		"Krust",
		"Kusarm",
		"Mald",
		"Margez",
		"Mol",
		"Muzminzi",
		"Ogokk",
		"Ohlzesh",
		"Okmekk",
		"Omzornust",
		"Ortom",
		"Ral",
		"Rern",
		"Rik",
		"Rimdesh",
		"Rirl",
		"Rogosh",
		"Rokzi",
		"Rozomich",
		"Rultoth",
		"Rumirl",
		"Rust",
		"Salturbenk",
		"Suzer",
		"Thangok",
		"Thank",
		"Thasgo",
		"Thestum",
		"Thihlgakk",
		"Thilitarg",
		"Thimos",
		"Thornoru",
		"Thrank",
		"Thrath",
		"Threldeth",
		"Threru",
		"Threterm",
		"Throndim",
		"Throrzurm",
		"Thrukonk",
		"Tresil",
		"Trok",
		"Trologzesh",
		"Truve",
		"Udarm",
		"Unostom",
		"Uromust",
		"Utzondem",
		"Zem",
		"Zimich",
		"Zovan",
	};

	NameMap[std::make_pair(RACE_ORC, GENDER_FEMALE)] =
	{
		"Ahdi",
		"Akiti",
		"Alu",
		"Ana",
		"Are",
		"Arthahka",
		"Asu",
		"Atdi",
		"Atharda",
		"Aweka",
		"Aze",
		"Azi",
		"Egomgy",
		"Ehzu",
		"Emo",
		"Fahkyr",
		"Faly",
		"Fatgi",
		"Fawo",
		"Foshi",
		"Fotde",
		"Fotrula",
		"Fuhgo",
		"Gada",
		"Gamu",
		"Garga",
		"Gehko",
		"Geme",
		"Gemzi",
		"Gewga",
		"Graltiwta",
		"Grehka",
		"Grehti",
		"Grere",
		"Grezyr",
		"Grotza",
		"Guza",
		"Ingenku",
		"Ini",
		"Isto",
		"Iszi",
		"Iwe",
		"Kagtide",
		"Kahe",
		"Kalte",
		"Karnu",
		"Karza",
		"Kegelu",
		"Kerte",
		"Ketes",
		"Kihmi",
		"Kinta",
		"Kuhke",
		"Madi",
		"Mahtatry",
		"Maloso",
		"Marno",
		"Mermet",
		"Mesdet",
		"Monmi",
		"Ode",
		"Oduny",
		"Ohku",
		"Omde",
		"Omzi",
		"Otgis",
		"Ramo",
		"Reshi",
		"Rimdu",
		"Rimot",
		"Ritu",
		"Sedilza",
		"Semezi",
		"Sere",
		"Sezulas",
		"Shahki",
		"Shehe",
		"Sholam",
		"Shuse",
		"Sihky",
		"Sohga",
		"Suma",
		"Suwza",
		"Tadditgi",
		"Tahtim",
		"Temzy",
		"Tendetgy",
		"Tidi",
		"Tiwidty",
		"Tohto",
		"Totowge",
		"Undi",
		"Unzy",
		"Utzemi",
		"Zemu",
		"Zohe",
		"Zohle",
		"Zumda",
		"Zutkisi",
		"Zutzy",
	};

	NameMap[std::make_pair(RACE_UNDEAD, GENDER_MALE)] =
	{
		"Alexei",
		"Alonso",
		"Amaury",
		"Amou",
		"Andrei",
		"Andrin",
		"Audric",
		"Baldemar",
		"Beall",
		"Bobby",
		"Braulio",
		"Brendon",
		"Bronwyn",
		"Bryan",
		"Cain",
		"Cecco",
		"Cedric",
		"Cicero",
		"Claus",
		"Clovis",
		"Constantin",
		"Darcio",
		"Darryll",
		"Dashawn",
		"David",
		"Delancy",
		"Dimitri",
		"Durwald",
		"Dwyghte",
		"Eckart",
		"Eriq",
		"Ernesto",
		"Ethelbert",
		"Falko",
		"Faust",
		"Faustino",
		"Ferd",
		"Ferdinand",
		"Frank",
		"Gascon",
		"Georg",
		"Gerrit",
		"Guillermo",
		"Hector",
		"Henley",
		"Heribert",
		"Hugbert",
		"Javon",
		"Jendrik",
		"Jenik",
		"Jochim",
		"Johannes",
		"Jones",
		"Jordy",
		"Justus",
		"Kennedy",
		"Kent",
		"Keven",
		"Kimon",
		"Lambrecht",
		"Langley",
		"Larry",
		"Libold",
		"Marcelo",
		"Marinus",
		"Maverick",
		"Mirko",
		"Moore",
		"Mortimer",
		"Nathanial",
		"Newbold",
		"Octavian",
		"Paulus",
		"Piero",
		"Piers",
		"Pruie",
		"Quesnel",
		"Ralph",
		"Reinwald",
		"Reynold",
		"Rubert",
		"Rudolfo",
		"Sammy",
		"Sean",
		"Stanleigh",
		"Tassilo",
		"Timon",
		"Torben",
		"Ursinus",
		"Ursio",
		"Valdimar",
		"Viktor",
		"Vilmos",
		"Vladimir",
		"Wes",
		"Wilmer",
		"Winsor",
		"Woodrow",
		"Zadoc",
		"Zaire",
	};

	NameMap[std::make_pair(RACE_UNDEAD, GENDER_FEMALE)] =
	{
		"Adelisa",
		"Alaina",
		"Alana",
		"Allete",
		"Alondra",
		"Amely",
		"Anita",
		"Arianna",
		"Arianne",
		"Aryanna",
		"Ashlin",
		"Aubine",
		"Aubry",
		"Ayla",
		"Bailey",
		"Betti",
		"Brooks",
		"Cathleen",
		"Cayla",
		"Celestyn",
		"Chantalle",
		"Charlette",
		"Chloe",
		"Clementia",
		"Coletta",
		"Darcelle",
		"Deanna",
		"Destine",
		"Devonne",
		"Diane",
		"Dorine",
		"Ellaine",
		"Elvire",
		"Emma",
		"Erika",
		"Ethelinda",
		"Euphrasia",
		"Felda",
		"Fernandina",
		"Fleta",
		"Friederike",
		"Germaine",
		"Graziella",
		"Hayden",
		"Hemma",
		"Hermia",
		"Hilma",
		"Hollye",
		"Jacqueline",
		"Jacqui",
		"Jaidyn",
		"Joyelle",
		"Kaelyn",
		"Kasey",
		"Keira",
		"Kimberly",
		"Konstanza",
		"Kyra",
		"Laney",
		"Laureen",
		"Lavonne",
		"Lena",
		"Leopolda",
		"Lorin",
		"Lurleen",
		"Madalynn",
		"Maidie",
		"Malenka",
		"Manda",
		"Marguerite",
		"Marsha",
		"Mckenna",
		"Michella",
		"Milva",
		"Nordica",
		"Palmiera",
		"Pasclina",
		"Philomela",
		"Richarda",
		"Richelle",
		"Rosalba",
		"Rosanna",
		"Rowena",
		"Ruby",
		"Serfine",
		"Shahana",
		"Shirley",
		"Sibylla",
		"Simonetta",
		"Sonia",
		"Steffi",
		"Stella",
		"Stormie",
		"Susana",
		"Tori",
		"Udele",
		"Violet",
		"Wandis",
		"Yara",
		"Yelena",
	};

	NameMap[std::make_pair(RACE_TAUREN, GENDER_MALE)] =
	{
		"Adenow",
		"Akando",
		"Apenimon",
		"Apiatan",
		"Ashkii",
		"Avonaco",
		"Ayawamat",
		"Behiwha",
		"Bopavik",
		"Bosto",
		"Budre",
		"Canhwe",
		"Catahecassa",
		"Ceanucha",
		"Chadoni",
		"Chaschunka",
		"Cherokee",
		"Chogan",
		"Chunta",
		"Dihwe",
		"Eachuzhnio",
		"Essi",
		"Etu",
		"Gize",
		"Gosheven",
		"Gutosumen",
		"Haja",
		"Hassun",
		"Helaku",
		"Honovi",
		"Hotah",
		"Huashki",
		"Illanipi",
		"Inanke",
		"Iskeave",
		"Itwa",
		"Jihni",
		"Jolon",
		"Kachada",
		"Kachna",
		"Kitchi",
		"Knewhocee",
		"Knoke",
		"Knoton",
		"Kwerak",
		"Kwonok",
		"Kwooake",
		"Kwuchna",
		"Kwuroo",
		"Leanoh",
		"Lidroch",
		"Lohwa",
		"Lonato",
		"Lootah",
		"Makya",
		"Maska",
		"Mazablaska",
		"Milap",
		"Motutwotak",
		"Mupo",
		"Muraco",
		"Nahiossi",
		"Nawkaw",
		"Ookli",
		"Pachu",
		"Payatt",
		"Petusmap",
		"Pobo",
		"Powwaw",
		"Qasioh",
		"Qinsechun",
		"Qochata",
		"Sogacke",
		"Suzhnau",
		"Takoda",
		"Tasunke",
		"Taza",
		"Teetonka",
		"Tohopka",
		"Tokala",
		"Tooahe",
		"Unnee",
		"Voinnokach",
		"Wahkan",
		"Waivunke",
		"Wamblee",
		"Wanikiy",
		"Waselsube",
		"Wehti",
		"Wikvaya",
		"Woago",
		"Wodzot",
		"Wussio",
		"Wuyi",
		"Yancy",
		"Yikudziki",
		"Yivo",
		"Yonyah",
		"Yuma",
		"Yushek",
	};

	NameMap[std::make_pair(RACE_TAUREN, GENDER_FEMALE)] =
	{
		"Adsila",
		"Alaqua",
		"Alawa",
		"Anevay",
		"Awenasa",
		"Bena",
		"Blumke",
		"Bomkua",
		"Cheblie",
		"Chekke",
		"Chickoa",
		"Chilletti",
		"Dorsheova",
		"Eenoe",
		"Eoza",
		"Esoha",
		"Eyota",
		"Flolgi",
		"Flowallien",
		"Flushi",
		"Fupe",
		"Gopinna",
		"Hausis",
		"Iomto",
		"Isi",
		"Kanti",
		"Karmiti",
		"Kaya",
		"Kegadi",
		"Kelli",
		"Kilenya",
		"Kimimela",
		"Kirima",
		"Kola",
		"Koleyna",
		"Kuwhantasi",
		"Kwebi",
		"Kwokiro",
		"Kwosie",
		"Linlu",
		"Liseli",
		"Mahala",
		"Mai",
		"Mechnap",
		"Miakoda",
		"Mikkim",
		"Mozhe",
		"Muna",
		"Nahiossi",
		"Nascha",
		"Neze",
		"Nisoe",
		"Nita",
		"Notkoa",
		"Nutki",
		"Okello",
		"Oneida",
		"Opa",
		"Orida",
		"Pamuy",
		"Permo",
		"Pukitse",
		"Ruzokoe",
		"Sahkyo",
		"Sapew",
		"Seltse",
		"Shagolom",
		"Shania",
		"Shiomku",
		"Sinopa",
		"Songam",
		"Tacincala",
		"Tadita",
		"Tadoho",
		"Tagamoia",
		"Tainn",
		"Takala",
		"Tallulah",
		"Tazanna",
		"Tebuspa",
		"Tello",
		"Tilari",
		"Tiru",
		"Tocho",
		"Tollankte",
		"Topanga",
		"Torsho",
		"Tungwo",
		"Tuwa",
		"Ucho",
		"Udi",
		"Urmo",
		"Vussacach",
		"Wematin",
		"Wigi",
		"Wuyi",
		"Yamka",
		"Yepa",
		"Zabana",
		"Zagisho",
	};

	NameMap[std::make_pair(RACE_TROLL, GENDER_MALE)] =
	{
		"Aehron",
		"Aesto",
		"Almuh",
		"Amuy",
		"Arsul",
		"Drisnan",
		"Drogsan",
		"Dromornon",
		"Druranian",
		"Hapta",
		"Hekjun",
		"Heljisun",
		"Hoxah",
		"Igujul",
		"Jaaghain",
		"Jagake",
		"Jahnok",
		"Jaigchuz",
		"Jaincha",
		"Jegthi",
		"Jehzok",
		"Jihghei",
		"Jizun",
		"Kagasir",
		"Kozken",
		"Kugan",
		"Kuson",
		"Laghohai",
		"Lagne",
		"Logna",
		"Logshano",
		"Luha",
		"Luzomur",
		"Maamu",
		"Maelgego",
		"Magtush",
		"Mawogah",
		"Mazen",
		"Metho",
		"Mihor",
		"Mogduz",
		"Momgipia",
		"Musa",
		"Obenal",
		"Ohen",
		"Olaz",
		"Pahthehun",
		"Pemaijun",
		"Pushu",
		"Raavaz",
		"Rhernal",
		"Rhilainu",
		"Rhirmek",
		"Rhisinay",
		"Rhozsuy",
		"Rhugtumo",
		"Rhuhdo",
		"Rhunghi",
		"Rhuthan",
		"Rigaloy",
		"Rogsoler",
		"Rongikon",
		"Rugriak",
		"Ruhkin",
		"Saelan",
		"Shostaipar",
		"Sogaerun",
		"Suhkan",
		"Taarmaso",
		"Taichon",
		"Tathaeren",
		"Tomboso",
		"Tonkulun",
		"Tregzaehan",
		"Trokinin",
		"Tzagtomo",
		"Tzashan",
		"Tzehon",
		"Tzihkay",
		"Tzingair",
		"Ulzon",
		"Urme",
		"Varsugen",
		"Vasnai",
		"Vobaron",
		"Volsaja",
		"Vurzoh",
		"Waamul",
		"Warsu",
		"Weda",
		"Wuzkiga",
		"Xihshogia",
		"Xoljun",
		"Xunay",
		"Zamtune",
		"Zasanih",
		"Zembun",
		"Zogzok",
		"Zuyin",
		"Zuzon",
	};

	NameMap[std::make_pair(RACE_TROLL, GENDER_FEMALE)] =
	{
		"Allenn",
		"Ama",
		"Antso",
		"Anza",
		"Baza",
		"Dejanda",
		"Diyici",
		"Ehmu",
		"Einza",
		"Ejo",
		"Elmon",
		"Enjocei",
		"Enke",
		"Feiluzi",
		"Fento",
		"Fildu",
		"Fohzi",
		"Foowulo",
		"Funniemea",
		"Gezuden",
		"Giawoth",
		"Ginimje",
		"Gunda",
		"Hahsiyie",
		"Helrunn",
		"Holma",
		"Hozjo",
		"Humghu",
		"Huse",
		"Huzozzo",
		"Inahlo",
		"Iwei",
		"Izkinn",
		"Janjoyo",
		"Jezraesho",
		"Jine",
		"Joragei",
		"Juga",
		"Kacidu",
		"Kaihrieh",
		"Khazki",
		"Khewe",
		"Khiehku",
		"Khuntsu",
		"Kontie",
		"Lahmiajo",
		"Laiden",
		"Laisizro",
		"Lasedo",
		"Lizjerar",
		"Loja",
		"Mahme",
		"Mandu",
		"Marujonn",
		"Mealtsojo",
		"Nejohe",
		"Neju",
		"Nolma",
		"Nozda",
		"Nucama",
		"Pelu",
		"Pihje",
		"Polu",
		"Pondor",
		"Pume",
		"Punza",
		"Ronjir",
		"Royelri",
		"Seldi",
		"Selja",
		"Shaye",
		"Shijun",
		"Shomjia",
		"Shorunwo",
		"Sotu",
		"Tajmin",
		"Tilzei",
		"Tohkei",
		"Tozdae",
		"Tusnetsa",
		"Ulzu",
		"Undei",
		"Vale",
		"Vedo",
		"Vilku",
		"Vinedu",
		"Voshnitsa",
		"Xanteh",
		"Xeikandi",
		"Xihmo",
		"Xuhjun",
		"Yaldir",
		"Yeira",
		"Yozmashi",
		"Zazan",
		"Zexath",
		"Zhajae",
		"Zhija",
		"Zhijein",
		"Zinku",
	};

	NameMap[std::make_pair(RACE_BLOODELF, GENDER_MALE)] =
	{
		"Alaaris",
		"Alamnath",
		"Alamthen",
		"Alnus",
		"Amornian",
		"Artheon",
		"Astarel",
		"Baash",
		"Bacaen",
		"Baemaen",
		"Baemlath",
		"Barad",
		"Bemaiel",
		"Bethos",
		"Cailion",
		"Camtalor",
		"Celoesil",
		"Celolen",
		"Celtis",
		"Colion",
		"Colmin",
		"Darlan",
		"Dartheol",
		"Duath",
		"Fanomar",
		"Geriel",
		"Gethelon",
		"Gevelion",
		"Hais",
		"Halen",
		"Halmin",
		"Harel",
		"Hatthaen",
		"Hedoran",
		"Idonnan",
		"Inethmir",
		"Inethranir",
		"Inmae",
		"Inthas",
		"Iththos",
		"Jentalor",
		"Kantalor",
		"Keelien",
		"Keelvedon",
		"Kelelis",
		"Larentalor",
		"Lelaris",
		"Matdron",
		"Mathaes",
		"Mathos",
		"Meletus",
		"Melevedon",
		"Melthas",
		"Meus",
		"Nernus",
		"Nerran",
		"Noraeranis",
		"Noraesh",
		"Osselemar",
		"Ossesen",
		"Panthis",
		"Pardis",
		"Parnus",
		"Parran",
		"Patheol",
		"Peden",
		"Peilan",
		"Perneas",
		"Pethul",
		"Peven",
		"Ponaleron",
		"Qualthis",
		"Quellan",
		"Quelsil",
		"Quthemar",
		"Raaen",
		"Saeesh",
		"Saetil",
		"Salthenis",
		"Sellion",
		"Selnomir",
		"Soiel",
		"Solerun",
		"Talran",
		"Tertalor",
		"Tynnar",
		"Tyris",
		"Tyrtor",
		"Vaenien",
		"Valranir",
		"Varlis",
		"Varnar",
		"Wearis",
		"Weeath",
		"Weeron",
		"Weleanis",
		"Weleeron",
		"Zanral",
		"Zedaras",
		"Zenan",
	};

	NameMap[std::make_pair(RACE_BLOODELF, GENDER_FEMALE)] =
	{
		"Aeldrea",
		"Alalene",
		"Amorea",
		"Auristra",
		"Azaerae",
		"Azaevea",
		"Belolinda",
		"Bemanise",
		"Bemasilla",
		"Cairae",
		"Caydrel",
		"Ceanice",
		"Ceemisa",
		"Ceerin",
		"Celthel",
		"Ceniel",
		"Dablestra",
		"Dalestra",
		"Deleane",
		"Denice",
		"Deyre",
		"Deyvia",
		"Ella",
		"Elwea",
		"Eresa",
		"Erithalis",
		"Erivia",
		"Erodana",
		"Falia",
		"Feywe",
		"Garne",
		"Garrea",
		"Garthalis",
		"Glozia",
		"Hadina",
		"Hali",
		"Hanridel",
		"Jaelina",
		"Jasara",
		"Jovirae",
		"Kalda",
		"Kalinda",
		"Kanarea",
		"Kanlinda",
		"Kealelda",
		"Kealriah",
		"Keestra",
		"Keldrin",
		"Keliria",
		"Kellia",
		"Kinalatha",
		"Laridana",
		"Lealenn",
		"Lidel",
		"Lilen",
		"Lilia",
		"Loralsia",
		"Lorana",
		"Lorari",
		"Lorridel",
		"Lyne",
		"Malania",
		"Marlia",
		"Menice",
		"Narerea",
		"Narilanne",
		"Nazara",
		"Noma",
		"Noviriah",
		"Olira",
		"Raedel",
		"Sadori",
		"Sarada",
		"Sedane Peacecloud",
		"Shali",
		"Sharlestra",
		"Sheline",
		"Silamine",
		"Silanna",
		"Sylene",
		"Syllania",
		"Tadine",
		"Tanlaya",
		"Tanlia",
		"Tera",
		"Titania",
		"Tydrea",
		"Tyenice",
		"Tylenne",
		"Tynia",
		"Valine",
		"Vela",
		"Velawae",
		"Veleane",
		"Zaerae",
		"Zalrianna",
		"Zarlenne",
		"Zarrel",
		"Zarsia",
		"Zatine",
	};
}

/*********************************************************/
/***                    CHAT COMMANDS                  ***/
/*********************************************************/

static std::map<std::string, Classes> argClasses = // NOLINT
{
	{ "warrior",	CLASS_WARRIOR	},
	{ "paladin",	CLASS_PALADIN	},
	{ "hunter",		CLASS_HUNTER	},
	{ "rogue",		CLASS_ROGUE		},
	{ "priest",     CLASS_PRIEST	},
	{ "shaman",		CLASS_SHAMAN	},
	{ "mage",		CLASS_MAGE		},
	{ "warlock",    CLASS_WARLOCK	},
	{ "druid",		CLASS_DRUID		},
};

static std::map<std::string, CombatBotRoles> argRoles = // NOLINT
{
	{ "tank",		ROLE_TANK		},
	{ "healer",		ROLE_HEALER		},
	{ "mdps",		ROLE_MELEE_DPS	},
	{ "rdps",		ROLE_RANGE_DPS	},
};

static std::map<std::string, RaidTargetIcon> raidTargetIcons = // NOLINT
{
	{ "star",     RAID_TARGET_ICON_STAR     },
	{ "circle",   RAID_TARGET_ICON_CIRCLE   },
	{ "diamond",  RAID_TARGET_ICON_DIAMOND  },
	{ "triangle", RAID_TARGET_ICON_TRIANGLE },
	{ "moon",     RAID_TARGET_ICON_MOON     },
	{ "square",   RAID_TARGET_ICON_SQUARE   },
	{ "cross",    RAID_TARGET_ICON_CROSS    },
	{ "skull",    RAID_TARGET_ICON_SKULL    },
};

static std::map<RaidTargetIcon, std::string> raidTargetIconsColors = // NOLINT
{
	{ RAID_TARGET_ICON_STAR,		DO_COLOR(COLOR_STAR, "star")},
	{ RAID_TARGET_ICON_CIRCLE,		DO_COLOR(COLOR_CIRCLE, "circle")},
	{ RAID_TARGET_ICON_DIAMOND,		DO_COLOR(COLOR_DIAMOND, "diamond")},
	{ RAID_TARGET_ICON_TRIANGLE,	DO_COLOR(COLOR_GREEN, "triangle")},
	{ RAID_TARGET_ICON_MOON,		DO_COLOR(COLOR_MOON, "moon")},
	{ RAID_TARGET_ICON_SQUARE,		DO_COLOR(COLOR_SQUARE, "square")},
	{ RAID_TARGET_ICON_CROSS,		DO_COLOR(COLOR_CROSS, "cross")},
	{ RAID_TARGET_ICON_SKULL,		DO_COLOR(COLOR_SILVER, "skull")},
};

CombatBotRoles SelectSpecificRoleForClass(const uint8 playerClass, const CombatBotRoles playerRole)
{
	switch (playerClass)
	{
	case CLASS_WARRIOR:
	{
		if (playerRole == ROLE_TANK || playerRole == ROLE_MELEE_DPS)
			return playerRole;

		return ROLE_MELEE_DPS;
	}
	case CLASS_PALADIN:
	{
		if (playerRole == ROLE_TANK || playerRole == ROLE_HEALER || playerRole == ROLE_MELEE_DPS)
			return playerRole;

		return ROLE_MELEE_DPS;
	}
	case CLASS_HUNTER:
	{
		return ROLE_RANGE_DPS;
	}
	case CLASS_ROGUE:
	{
		return ROLE_MELEE_DPS;
	}
	case CLASS_PRIEST:
	{
		if (playerRole == ROLE_HEALER || playerRole == ROLE_RANGE_DPS)
			return playerRole;

		return ROLE_RANGE_DPS;
	}
	case CLASS_SHAMAN:
	{
		if (playerRole == ROLE_TANK || playerRole == ROLE_HEALER || playerRole == ROLE_MELEE_DPS || playerRole == ROLE_RANGE_DPS)
			return playerRole;

		return ROLE_RANGE_DPS;
	}
	case CLASS_MAGE:
	case CLASS_WARLOCK:
	{
		return ROLE_RANGE_DPS;
	}
	case CLASS_DRUID:
	{
		if (playerRole == ROLE_TANK || playerRole == ROLE_HEALER || playerRole == ROLE_MELEE_DPS || playerRole == ROLE_RANGE_DPS)
			return playerRole;

		return ROLE_RANGE_DPS;
	}
	}

	return ROLE_INVALID;
}

uint8 SelectRandomRaceForClass(const uint8 playerClass, const Team playerTeam)
{
	switch (playerClass)
	{
	case CLASS_WARRIOR:
	{
		if (playerTeam == ALLIANCE)
			return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME, RACE_DRAENEI);

		return PickRandomValue(RACE_ORC, RACE_UNDEAD, RACE_TAUREN, RACE_TROLL);
	}
	case CLASS_PALADIN:
	{
		if (playerTeam == ALLIANCE)
			return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_DRAENEI);

		return RACE_BLOODELF;
	}
	case CLASS_HUNTER:
	{
		if (playerTeam == ALLIANCE)
			return PickRandomValue(RACE_NIGHTELF, RACE_DWARF, RACE_DRAENEI);

		return PickRandomValue(RACE_ORC, RACE_TAUREN, RACE_TROLL, RACE_BLOODELF);
	}
	case CLASS_ROGUE:
	{
		if (playerTeam == ALLIANCE)
			return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME);

		return PickRandomValue(RACE_ORC, RACE_UNDEAD, RACE_TROLL, RACE_BLOODELF);
	}
	case CLASS_PRIEST:
	{
		if (playerTeam == ALLIANCE)
			return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_DRAENEI);

		return PickRandomValue(RACE_BLOODELF, RACE_UNDEAD, RACE_TROLL);
	}
	case CLASS_SHAMAN:
	{
		if (playerTeam == HORDE)
			return PickRandomValue(RACE_ORC, RACE_TAUREN, RACE_TROLL);

		return RACE_DRAENEI;
	}
	case CLASS_MAGE:
	{
		if (playerTeam == ALLIANCE)
			return PickRandomValue(RACE_HUMAN, RACE_GNOME, RACE_DRAENEI);

		return PickRandomValue(RACE_UNDEAD, RACE_TROLL, RACE_BLOODELF);
	}
	case CLASS_WARLOCK:
	{
		if (playerTeam == ALLIANCE)
			return urand(0, 1) ? RACE_HUMAN : RACE_GNOME;

		return PickRandomValue(RACE_ORC, RACE_UNDEAD, RACE_BLOODELF);
	}
	case CLASS_DRUID:
	{
		return playerTeam == ALLIANCE ? RACE_NIGHTELF : RACE_TAUREN;
	}
	}

	return 0;
}

uint8 SelectSpecificRaceForClass(const uint8 playerClass, const Team playerTeam, const uint8 playerRace)
{
	switch (playerClass)
	{
	case CLASS_WARRIOR:
	{
		if (playerTeam == ALLIANCE)
		{
			if (playerRace == RACE_HUMAN || playerRace == RACE_DWARF || playerRace == RACE_NIGHTELF || playerRace == RACE_GNOME || playerRace == RACE_DRAENEI)
				return playerRace;

			return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME, RACE_DRAENEI);
		}

		if (playerRace == RACE_ORC || playerRace == RACE_UNDEAD || playerRace == RACE_TAUREN || playerRace == RACE_TROLL)
			return playerRace;

		return PickRandomValue(RACE_ORC, RACE_UNDEAD, RACE_TAUREN, RACE_TROLL);
	}
	case CLASS_PALADIN:
	{
		if (playerTeam == ALLIANCE)
		{
			if (playerRace == RACE_HUMAN || playerRace == RACE_DWARF || playerRace == RACE_DRAENEI)
				return playerRace;

			return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_DRAENEI);
		}

		if (playerRace == RACE_BLOODELF)
			return playerRace;

		return RACE_BLOODELF;
	}
	case CLASS_HUNTER:
	{
		if (playerTeam == ALLIANCE)
		{
			if (playerRace == RACE_DWARF || playerRace == RACE_NIGHTELF || playerRace == RACE_DRAENEI)
				return playerRace;

			return PickRandomValue(RACE_DWARF, RACE_NIGHTELF, RACE_DRAENEI);
		}

		if (playerRace == RACE_ORC || playerRace == RACE_TAUREN || playerRace == RACE_TROLL || playerRace == RACE_BLOODELF)
			return playerRace;

		return PickRandomValue(RACE_ORC, RACE_TAUREN, RACE_TROLL, RACE_BLOODELF);
	}
	case CLASS_ROGUE:
	{
		if (playerTeam == ALLIANCE)
		{
			if (playerRace == RACE_HUMAN || playerRace == RACE_DWARF || playerRace == RACE_NIGHTELF || playerRace == RACE_GNOME)
				return playerRace;

			return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME);
		}

		if (playerRace == RACE_ORC || playerRace == RACE_UNDEAD || playerRace == RACE_TROLL || playerRace == RACE_BLOODELF)
			return playerRace;

		return PickRandomValue(RACE_ORC, RACE_UNDEAD, RACE_TROLL, RACE_BLOODELF);
	}
	case CLASS_PRIEST:
	{
		if (playerTeam == ALLIANCE)
		{
			if (playerRace == RACE_HUMAN || playerRace == RACE_DWARF || playerRace == RACE_NIGHTELF || playerRace == RACE_DRAENEI)
				return playerRace;

			return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_DRAENEI);
		}

		if (playerRace == RACE_UNDEAD || playerRace == RACE_TROLL || playerRace == RACE_BLOODELF)
			return playerRace;

		return PickRandomValue(RACE_UNDEAD, RACE_TROLL, RACE_BLOODELF);
	}
	case CLASS_SHAMAN:
	{
		if (playerTeam == HORDE)
		{
			if (playerRace == RACE_ORC || playerRace == RACE_TAUREN || playerRace == RACE_TROLL)
				return playerRace;

			return PickRandomValue(RACE_ORC, RACE_TAUREN, RACE_TROLL);
		}

		if (playerRace == RACE_DRAENEI)
			return playerRace;

		return RACE_DRAENEI;
	}
	case CLASS_MAGE:
	{
		if (playerTeam == ALLIANCE)
		{
			if (playerRace == RACE_HUMAN || playerRace == RACE_GNOME || playerRace == RACE_DRAENEI)
				return playerRace;

			return PickRandomValue(RACE_HUMAN, RACE_GNOME, RACE_DRAENEI);
		}

		if (playerRace == RACE_UNDEAD || playerRace == RACE_TROLL || playerRace == RACE_BLOODELF)
		{
			return playerRace;
		}

		return PickRandomValue(RACE_UNDEAD, RACE_TROLL, RACE_BLOODELF);
	}
	case CLASS_WARLOCK:
	{
		if (playerTeam == ALLIANCE)
		{
			if (playerRace == RACE_HUMAN || playerRace == RACE_GNOME)
				return playerRace;

			return urand(0, 1) ? RACE_HUMAN : RACE_GNOME;
		}

		if (playerRace == RACE_ORC || playerRace == RACE_UNDEAD || playerRace == RACE_BLOODELF)
			return playerRace;

		return PickRandomValue(RACE_ORC, RACE_UNDEAD, RACE_BLOODELF);
	}
	case CLASS_DRUID:
	{
		if (playerTeam == ALLIANCE)
		{
			if (playerRace == RACE_NIGHTELF)
				return playerRace;

			return RACE_NIGHTELF;
		}

		if (playerRace == RACE_TAUREN)
		{
			return playerRace;
		}

		return RACE_TAUREN;
	}
	}

	return 0;
}

bool StopPartyBotAttackHelper(Player* pBot)
{
	if (!pBot) return false;
	if (const auto pAI = dynamic_cast<PartyBotAI*>(pBot->AI_NYCTERMOON()))
	{
		if (pBot->IsAlive() && !pBot->IsTaxiFlying() && pBot->IsInWorld() && !pBot->IsBeingTeleported())
		{
			pBot->AttackStop(false);
			pBot->InterruptNonMeleeSpells(false);
			if (!pBot->IsStopped())
				pBot->StopMoving();
			if (pBot->GetMotionMaster()->GetCurrentMovementGeneratorType())
			{
				pBot->GetMotionMaster()->Clear(false);
				pBot->GetMotionMaster()->MoveIdle();
			}
			if (Pet* pPet = pBot->GetPet())
			{
				pPet->AttackStop(false);
				pPet->InterruptNonMeleeSpells(false);
			}
			if (pAI->m_updateTimer.GetExpiry() < sPlayerBotMgr.BOT_UPDATE_INTERVAL)
				pAI->m_updateTimer.Reset(sPlayerBotMgr.BOT_UPDATE_INTERVAL);
			return true;
		}
	}
	return false;
}

bool HandlePartyBotComeAndStayHelper(Player* pBot, const Player* pPlayer)
{
	if (const auto pAI = dynamic_cast<PartyBotAI*>(pBot->AI_NYCTERMOON()))
	{
		// Don't allow comps who are too far away to follow
		if (!pBot->IsWithinDistInMap(pPlayer, 100.0f))
			return false;

		// Check if I have a custom follow
		if (pAI->m_follow)
			pPlayer = pAI->m_follow;

		if (!pBot->HasAuraType(SPELL_AURA_MOD_ROOT) && pBot->IsAlive() && pBot->IsInMap(pPlayer) && pBot->IsInWorld() && !pBot->IsBeingTeleported() && (!pBot->hasUnitState(UNIT_STAT_NO_FREE_MOVE) || pBot->hasUnitState(UNIT_STAT_ROOT)))
		{
			if (pBot->GetDistance(pPlayer) > 0.0f)
			{
				if (pAI->IsStaying)
				{
					pBot->clearUnitState(UNIT_STAT_ROOT);
					pAI->IsStaying = false;
				}

				if (pBot->GetVictim())
					StopPartyBotAttackHelper(pBot);

				if (Pet* pPet = pBot->GetPet())
				{
					if (pPet->IsAlive()) {
						pPet->AttackStop();
						pPet->InterruptNonMeleeSpells(false);
						if (CharmInfo* PetCharmInfo = pPet->GetCharmInfo())
							PetCharmInfo->SetCommandState(COMMAND_FOLLOW);
						if (pPet->GetDistance(pPlayer) > 0.0f)
							pPet->MonsterMove(pPlayer->GetPositionX(), pPlayer->GetPositionY(), pPlayer->GetPositionZ());
					}
				}
				if (pBot->getStandState() != UNIT_STAND_STATE_STAND)
					pBot->SetStandState(UNIT_STAND_STATE_STAND);
				pBot->InterruptSpellsAndAurasWithInterruptFlags(SPELL_INTERRUPT_FLAG_MOVEMENT);
				pBot->InterruptSpellsAndAurasWithInterruptFlags(SPELL_INTERRUPT_FLAG_MOVEMENT);

				pAI->m_come_location = true;
				pAI->m_come_location_x = pPlayer->GetPositionX();
				pAI->m_come_location_y = pPlayer->GetPositionY();
				pAI->m_come_location_z = pPlayer->GetPositionZ();
				pBot->MonsterMove(pPlayer->GetPositionX(), pPlayer->GetPositionY(), pPlayer->GetPositionZ());
			}
			pBot->addUnitState(UNIT_STAT_ROOT);
			pAI->IsStaying = true;
			return true;
		}
	}

	return false;
}

bool HandlePartyBotComeToMeHelper(Player* pBot, const Player* pPlayer)
{
	if (const auto pAI = dynamic_cast<PartyBotAI*>(pBot->AI_NYCTERMOON()))
	{
		// Don't allow comps who are too far away to follow
		if (!pBot->IsWithinDistInMap(pPlayer, 100.0f))
			return false;

		// Check if I have a custom follow
		if (pAI->m_follow)
			pPlayer = pAI->m_follow;

		if (!pBot->HasAuraType(SPELL_AURA_MOD_ROOT) && pBot->IsAlive() && pBot->IsInMap(pPlayer) && pBot->IsInWorld() && !pBot->IsBeingTeleported() && (!pBot->hasUnitState(UNIT_STAT_NO_FREE_MOVE) || pBot->hasUnitState(UNIT_STAT_ROOT)))
		{
			if (pBot->GetDistance(pPlayer) > 0.0f)
			{
				if (pBot->GetVictim())
					StopPartyBotAttackHelper(pBot);

				if (Pet* pPet = pBot->GetPet())
				{
					if (pPet->IsAlive())
					{
						pPet->AttackStop();
						pPet->InterruptNonMeleeSpells(false);
						if (CharmInfo* PetCharmInfo = pPet->GetCharmInfo())
							PetCharmInfo->SetCommandState(COMMAND_FOLLOW);
						if (pPet->GetDistance(pPlayer) > 0.0f)
							pPet->MonsterMove(pPlayer->GetPositionX(), pPlayer->GetPositionY(), pPlayer->GetPositionZ());
					}
				}

				if (pBot->getStandState() != UNIT_STAND_STATE_STAND)
					pBot->SetStandState(UNIT_STAND_STATE_STAND);

				pBot->InterruptSpellsAndAurasWithInterruptFlags(SPELL_INTERRUPT_FLAG_MOVEMENT);

				pAI->m_come_location = true;
				pAI->m_come_location_x = pPlayer->GetPositionX();
				pAI->m_come_location_y = pPlayer->GetPositionY();
				pAI->m_come_location_z = pPlayer->GetPositionZ();
				pBot->MonsterMove(pPlayer->GetPositionX(), pPlayer->GetPositionY(), pPlayer->GetPositionZ());
			}
			return true;
		}
	}

	return false;
}

bool HandlePartyBotPauseApplyHelper(Player* pTarget, const uint32 duration)
{
	if (auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON()))
	{
		if (!pTarget->IsTaxiFlying() && pTarget->IsInWorld() && !pTarget->IsBeingTeleported())
		{
			pAI->m_updateTimer.Reset(duration);
			if (duration > sPlayerBotMgr.BOT_UPDATE_INTERVAL)
			{
				// Stop attack
				pTarget->AttackStop(false);
				pTarget->InterruptNonMeleeSpells(false);
				if (pTarget->IsMoving())
					pTarget->StopMoving();
				if (pTarget->GetMotionMaster()->GetCurrentMovementGeneratorType())
				{
					pTarget->GetMotionMaster()->Clear(false);
					pTarget->GetMotionMaster()->MoveIdle();
				}

				if (Pet* pPet = pTarget->GetPet())
				{
					if (pPet->IsAlive())
					{
						pPet->AttackStop(false);
						pPet->InterruptNonMeleeSpells(false);
						if (CharmInfo* PetCharmInfo = pPet->GetCharmInfo())
							PetCharmInfo->SetCommandState(COMMAND_FOLLOW);
					}
				}
			}
			return true;
		}
	}
	return false;
}

bool HandlePartyBotStayHelper(Player* pBot)
{
	if (auto pAI = dynamic_cast<PartyBotAI*>(pBot->AI_NYCTERMOON()))
	{
		if (pBot->IsAlive() && !pBot->IsTaxiFlying() && pBot->IsInWorld() && !pBot->IsBeingTeleported())
		{
			if (!pBot->IsStopped())
				pBot->StopMoving();
			if (pBot->GetMotionMaster()->GetCurrentMovementGeneratorType())
			{
				pBot->GetMotionMaster()->Clear(false);
				pBot->GetMotionMaster()->MoveIdle();
			}
			pBot->addUnitState(UNIT_STAT_ROOT);
			pAI->IsStaying = true;
			return true;
		}
	}
	return false;
}

bool HandlePartyBotMoveHelper(Player* pBot)
{
	if (const auto pAI = dynamic_cast<PartyBotAI*>(pBot->AI_NYCTERMOON()))
	{
		if (!pBot->HasAuraType(SPELL_AURA_MOD_ROOT) && pBot->IsAlive() && pBot->IsInWorld() && !pBot->IsBeingTeleported() && pAI->IsStaying)
		{
			if (pBot->hasUnitState(UNIT_STAT_ROOT))
				pBot->clearUnitState(UNIT_STAT_ROOT);
			pAI->IsStaying = false;
			return true;
		}
	}
	return false;
}

bool HandlePartyBotToggleHelper(Player* pBot, const Player* pPlayer, const uint8 choice = 3)
{
	if (pBot->AI_NYCTERMOON() && pBot->IsAlive() && pBot->IsInMap(pPlayer))
	{
		if (choice == 1)
		{
			if (pBot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM))
				pBot->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);
			else
				pBot->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);

			return true;
		}

		if (choice == 2)
		{
			if (pBot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK))
				pBot->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);
			else
				pBot->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);

			return true;
		}

		if (choice == 3)
		{
			if (pBot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM))
				pBot->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);
			else
				pBot->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);

			if (pBot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK))
				pBot->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);
			else
				pBot->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);

			return true;
		}
	}

	return false;
}

uint8 HandlePartyBotComeToggleHelper(Player* pBot, const Player* pPlayer)
{
	if (const auto pAI = dynamic_cast<PartyBotAI*>(pBot->AI_NYCTERMOON()))
	{
		// Check if I have a custom follow
		if (pAI->m_follow)
			pPlayer = pAI->m_follow;

		if (pBot->IsAlive() && !pBot->IsTaxiFlying() && pBot->IsInWorld() && !pBot->IsBeingTeleported())
		{
			// Interrupt attacks
			if (pBot->GetDistance(pPlayer) > 0.0f)
			{
				if (pBot->GetVictim())
					StopPartyBotAttackHelper(pBot);

				if (Pet* pPet = pBot->GetPet())
				{
					if (pPet->IsAlive())
					{
						pPet->AttackStop();
						pPet->InterruptNonMeleeSpells(false);
						if (CharmInfo* PetCharmInfo = pPet->GetCharmInfo())
							PetCharmInfo->SetCommandState(COMMAND_FOLLOW);
						if (pPet->GetDistance(pPlayer) > 0.0f)
							pPet->MonsterMove(pPlayer->GetPositionX(), pPlayer->GetPositionY(), pPlayer->GetPositionZ());
					}
				}

				if (pBot->getStandState() != UNIT_STAND_STATE_STAND)
					pBot->SetStandState(UNIT_STAND_STATE_STAND);

				pBot->InterruptSpellsAndAurasWithInterruptFlags(SPELL_INTERRUPT_FLAG_MOVEMENT);
			}

			// Toggle on
			if (!pAI->m_toggle_come)
			{
				if (pBot->CanFreeMove() && !pBot->HasAuraType(SPELL_AURA_MOD_ROOT) && !pBot->hasUnitState(UNIT_STAT_ROOT) && pBot->IsInMap(pPlayer) && pBot->IsInWorld() && !pBot->IsBeingTeleported())
				{
					pBot->GetMotionMaster()->Clear(false);
					pBot->GetMotionMaster()->MovePoint(1, pPlayer->GetPositionX(), pPlayer->GetPositionY(), pPlayer->GetPositionZ(), FORCED_MOVEMENT_RUN);
				}
				pAI->m_toggle_come = true;
				return 1;
			}

			// Toggle off
			if (pAI->m_toggle_come)
			{
				if (!pAI->IsStaying && pBot->hasUnitState(UNIT_STAT_ROOT))
					pBot->clearUnitState(UNIT_STAT_ROOT);
				pAI->m_toggle_come = false;
				return 2;
			}
		}
	}

	return 0;
}

uint8 HandlePartyBotToggleAOEHelper(Player* pBot)
{
	if (const auto pAI = dynamic_cast<PartyBotAI*>(pBot->AI_NYCTERMOON()))
	{
		if (pAI->m_allow_aoe)
		{
			pAI->m_allow_aoe = false;
			return 1;
		}

		if (!pAI->m_allow_aoe)
		{
			pAI->m_allow_aoe = true;
			return 2;
		}
	}

	return 0;
}

uint8 HandlePartyBotToggleTotemsHelper(Player* pBot)
{
	if (const auto pAI = dynamic_cast<PartyBotAI*>(pBot->AI_NYCTERMOON()))
	{
		if (pAI->m_allow_totems)
		{
			pAI->m_allow_totems = false;
			return 1;
		}

		if (!pAI->m_allow_totems)
		{
			pAI->m_allow_totems = true;
			return 2;
		}
	}

	return 0;
}

uint8 HandlePartyBotComeToggleHelper(Player* pBot, Player* pPlayer)
{
	if (const auto pAI = dynamic_cast<PartyBotAI*>(pBot->AI_NYCTERMOON()))
	{
		// Check if I have a custom follow
		if (pAI->m_follow)
			pPlayer = pAI->m_follow;

		if (pBot->IsAlive() && !pBot->IsTaxiFlying() && pBot->IsInWorld() && !pBot->IsBeingTeleported())
		{
			// Interrupt attacks
			if (pBot->GetDistance(pPlayer) > 0.0f)
			{
				if (pBot->GetVictim())
					StopPartyBotAttackHelper(pBot);

				if (Pet* pPet = pBot->GetPet())
				{
					if (pPet->IsAlive())
					{
						pPet->AttackStop();

						pPet->InterruptNonMeleeSpells(false);
						if (CharmInfo* PetCharmInfo = pPet->GetCharmInfo())
							PetCharmInfo->SetCommandState(COMMAND_FOLLOW);

						if (pPet->GetDistance(pPlayer) > 0.0f)
							pPet->MonsterMove(pPlayer->GetPositionX(), pPlayer->GetPositionY(), pPlayer->GetPositionZ());
					}
				}

				if (pBot->getStandState() != UNIT_STAND_STATE_STAND)
					pBot->SetStandState(UNIT_STAND_STATE_STAND);

				pBot->InterruptSpellsAndAurasWithInterruptFlags(SPELL_INTERRUPT_FLAG_MOVEMENT);
			}

			// Toggle on
			if (!pAI->m_toggle_come)
			{
				if (pBot->CanFreeMove() && !pBot->HasAuraType(SPELL_AURA_MOD_ROOT) && !pBot->hasUnitState(UNIT_STAT_ROOT) && pBot->IsInMap(pPlayer) && pBot->IsInWorld() && !pBot->IsBeingTeleported())
				{
					pBot->GetMotionMaster()->Clear(false);
					pBot->GetMotionMaster()->MovePoint(1, pPlayer->GetPositionX(), pPlayer->GetPositionY(), pPlayer->GetPositionZ(), FORCED_MOVEMENT_RUN);
				}
				pAI->m_toggle_come = true;
				return 1;
			}

			// Toggle off
			if (pAI->m_toggle_come)
			{
				if (!pAI->IsStaying && pBot->hasUnitState(UNIT_STAT_ROOT))
					pBot->clearUnitState(UNIT_STAT_ROOT);
				pAI->m_toggle_come = false;
				return 2;
			}
		}
	}

	return 0;
}

uint8 HandlePartyBotUseGObjectHelper(Player* pTarget, GameObject* pGo, const Player* pLeader)
{
	if (const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON()))
		if (pAI->m_DND)
			return 0;

	// Don't allow Ritual of Summoning
	if (pGo->GetSpellId() == 698)
		return 0;

	if (pTarget->AI_NYCTERMOON() && !pTarget->IsTaxiFlying() && pTarget->IsInWorld() && !pTarget->IsBeingTeleported())
	{
		if (pTarget->IsWithinDist(pGo, INTERACTION_DISTANCE - 1.0f))
		{
			pTarget->SetFacingToObject(pGo);
			if (pTarget->CanInteract(pGo))
			{
				// BWL - Suppression Device
				if (pGo->GetGOInfo()->id == 179784 && pTarget->getClass() != CLASS_ROGUE)
					return 2;

				// Rogues open chests and doors
				if ((pGo->GetGoType() == GAMEOBJECT_TYPE_CHEST || pGo->GetGoType() == GAMEOBJECT_TYPE_DOOR) &&
					pGo->GetGoState() == GO_STATE_READY && pGo->GetGOInfo()->GetLockId() &&
					pTarget->getClass() == CLASS_ROGUE)
				{
					if (!pTarget->HasSpell(1804))
						return 4;

					if (pGo->GetGoType() == GAMEOBJECT_TYPE_CHEST)
					{
						// This makes the bot cast lockpicking on the chest
						//if (pTarget->CastSpell(pGo) == SPELL_CAST_OK) // TODO FIX THIS SHIT
						{
							const std::string chatResponse = "I have opened this chest for you.";
							pTarget->MonsterWhisper(chatResponse.c_str(), pLeader);

							// This sends the loot to the bot's master
							//pLeader->SendLootError(pGo->GetObjectGuid(), LOOT_SKINNING);
							return 3;
						}

						std::string chatResponse = "I can't open this chest.";
						pTarget->MonsterWhisper(chatResponse.c_str(), pLeader);

						LockEntry const* lock = sLockStore.LookupEntry(pGo->GetGOInfo()->GetLockId());
						if (pTarget->GetSkillValue(633) < lock->Skill[1])
						{
							chatResponse = "It requires " + std::to_string(lock->Skill[1]) + " Lockpicking Skill and I only have " + std::to_string(pTarget->GetSkillValue(633)) + ".";
							pTarget->MonsterWhisper(chatResponse.c_str(), pLeader);
						}
						return 0;
					}

					if (pGo->GetGoType() == GAMEOBJECT_TYPE_DOOR)
					{
						// This makes the bot cast lockpicking on the door
						//if (pTarget->CastSpell(pGo, sSpellMgr.GetSpellEntry(1804), false) == SPELL_CAST_OK)
						{
							const std::string chatResponse = "I have opened this door for you.";
							pTarget->MonsterWhisper(chatResponse.c_str(), pLeader);
							return 3;
						}

						std::string chatResponse = "I can't open this door.";
						pTarget->MonsterWhisper(chatResponse.c_str(), pLeader);

						LockEntry const* lock = sLockStore.LookupEntry(pGo->GetGOInfo()->GetLockId());
						if (pTarget->GetSkillValue(633) < lock->Skill[1])
						{
							chatResponse = "It requires " + std::to_string(lock->Skill[1]) + " Lockpicking Skill and I only have " + std::to_string(pTarget->GetSkillValue(633)) + ".";
							pTarget->MonsterWhisper(chatResponse.c_str(), pLeader);
						}
						return 0;
					}
				}

				// Don't let anyone else try to open locked doors
				if ((pGo->GetGoType() == GAMEOBJECT_TYPE_CHEST || pGo->GetGoType() == GAMEOBJECT_TYPE_DOOR) &&
					pGo->GetGOInfo()->GetLockId() && pTarget->getClass() != CLASS_ROGUE)
					return 2;

				// This is general object usage, like altars.
				pGo->Use(pTarget);
				return 3;
			}
		}
		//pTarget->GetMotionMaster()->MoveDistance(pLeader, 0.0f); // TODO FIX
		return 1;
	}

	return 0;
}

uint8 ChatHandler::GetCompanionCount(Player* pPlayer)
{
	if (Group* pGroup = pPlayer->GetGroup())
	{
		uint8 CompanionCount = 0;
		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
				{
					if (!pAI->IsClone && pAI->m_leaderGUID == pPlayer->GetObjectGuid())
					{
						CompanionCount++;
					}
				}
			}
		}

		return CompanionCount;
	}

	return 0;
}

bool ChatHandler::HandleBotUpdateInterval(char* args)
{
	if (const char* arg1 = ExtractArg(&args))
	{
		const std::string mode = arg1;
		if (mode == "manual")
		{
			if (const char* arg2 = ExtractArg(&args))
			{
				uint32 duration = atoi(arg2);
				if (duration < 100) duration = 100;
				sPlayerBotMgr.BOT_UPDATE_INTERVAL_MANUAL = true;
				sPlayerBotMgr.BOT_UPDATE_INTERVAL = duration;

				std::string chatMsg;
				chatMsg += DO_COLOR(COLOR_WHITE, "Bot update interval set to ");
				chatMsg += DO_COLOR(COLOR_BLUE, "%s.");
				PSendSysMessage(chatMsg.c_str(), std::to_string(duration).c_str());
				return true;
			}
		}
		else if (mode == "automatic")
		{
			sPlayerBotMgr.BOT_UPDATE_INTERVAL_MANUAL = false;

			std::string chatMsg;
			chatMsg += DO_COLOR(COLOR_WHITE, "Bot update interval set to ");
			chatMsg += DO_COLOR(COLOR_BLUE, "automatic.");
			PSendSysMessage(chatMsg.c_str());
			return true;
		}
	}
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::PartyBotAddRequirementCheck(Player* pPlayer, Player* pTarget)
{
	if (!pPlayer)
		return false;

	if (pPlayer->IsHiringCompanion)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "Wait until the other ") DO_COLOR(COLOR_ORANGE, "Companion") DO_COLOR(COLOR_WHITE, " has been initialized."));
		return false;
	}

	if (pPlayer->IsTaxiFlying())
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "Cannot hire ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " while flying."));
		return false;
	}

	if (pPlayer->InBattleGround())
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "Cannot hire ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " inside Battlegrounds."));
		return false;
	}

	if (pPlayer->GetGroup() && pPlayer->GetGroup()->IsFull())
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "Cannot hire any more ") DO_COLOR(COLOR_ORANGE, "Companions.") DO_COLOR(COLOR_WHITE, " Group is full."));
		return false;
	}

	if (pPlayer->GetGroup() && GetCompanionCount(pPlayer) >= 4 && pPlayer->GetSession()->GetSecurity() < SEC_ADMINISTRATOR)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "Cannot hire any more ") DO_COLOR(COLOR_ORANGE, "Companions.") DO_COLOR(COLOR_WHITE, " The maximum is 4."));
		return false;
	}

	if (Map const* pMap = pPlayer->GetMap())
	{
		if (pMap->IsDungeon() &&
			pMap->GetPlayers().getSize() >= pMap->GetMaxPlayers())
		{
			SendSysMessage(DO_COLOR(COLOR_WHITE, "Cannot hire any more ") DO_COLOR(COLOR_ORANGE, "Companions.") DO_COLOR(COLOR_WHITE, " Instance is full."));
			return false;
		}
	}

	if (pTarget && pTarget->GetTeam() != pPlayer->GetTeam())
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "Cannot clone enemy faction characters."));
		return false;
	}

	// Restrictions when the command is made public to avoid abuse.
	if (GetSession()->GetSecurity() <= SEC_PLAYER)
	{
		if (pPlayer->IsDead())
		{
			SendSysMessage(DO_COLOR(COLOR_WHITE, "Cannot hire ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " while dead."));
			return false;
		}

		if (pPlayer->IsInCombat())
		{
			SendSysMessage(DO_COLOR(COLOR_WHITE, "Cannot hire ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " while in combat."));
			return false;
		}

		if (pPlayer->GetMap()->IsDungeon())
		{
			SendSysMessage(DO_COLOR(COLOR_WHITE, "Cannot hire ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " while inside instances."));
			return false;
		}

		// Clone command.
		if (pTarget)
		{
			if (pTarget->IsDead())
			{
				SendSysMessage(DO_COLOR(COLOR_WHITE, "Cannot clone dead characters."));
				return false;
			}

			if (pTarget->IsInCombat())
			{
				SendSysMessage(DO_COLOR(COLOR_WHITE, "Cannot clone characters that are in combat."));
				return false;
			}

			if (pTarget->GetLevel() > pPlayer->GetLevel())
			{
				SendSysMessage(DO_COLOR(COLOR_WHITE, "Cannot clone higher level characters."));
				return false;
			}

			if (pTarget->AI_NYCTERMOON())
			{
				SendSysMessage(DO_COLOR(COLOR_WHITE, "You may only clone Players."));
				return false;
			}
		}
	}

	return true;
}

bool ChatHandler::HandlePartyBotAddCommand(char* args)
{
	Player* pPlayer = m_session->GetPlayer();
	if (!pPlayer)
		return false;

	if (!args)
	{
		SendSysMessage("Incorrect syntax. Expected role or class.");
		SetSentErrorMessage(true);
		return false;
	}

	uint8 botClass = 0;
	CombatBotRoles botRole = ROLE_INVALID;
	CombatBotRoles botRoleOption = ROLE_INVALID;
	uint8 botRace = 0;
	uint32 botLevel = pPlayer->GetLevel();
	if (const char* arg1 = ExtractArg(&args))
	{
		const std::string option = arg1;
		if (option == "warrior")
			botClass = CLASS_WARRIOR;
		else if (option == "paladin")
			botClass = CLASS_PALADIN;
		else if (option == "hunter")
			botClass = CLASS_HUNTER;
		else if (option == "rogue")
			botClass = CLASS_ROGUE;
		else if (option == "priest")
			botClass = CLASS_PRIEST;
		else if (option == "shaman")
			botClass = CLASS_SHAMAN;
		else if (option == "mage")
			botClass = CLASS_MAGE;
		else if (option == "warlock")
			botClass = CLASS_WARLOCK;
		else if (option == "druid")
			botClass = CLASS_DRUID;
		else if (option == "dps")
		{
			const std::vector<uint32> dpsClasses = { CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_MAGE, CLASS_WARLOCK, CLASS_DRUID, CLASS_PRIEST,CLASS_SHAMAN,CLASS_PALADIN };
			botClass = static_cast<uint8>(SelectRandomContainerElement(dpsClasses));
			botRole = CombatBotBaseAI::IsMeleeDamageClass(botClass) ? ROLE_MELEE_DPS : ROLE_RANGE_DPS;
		}
		else if (option == "healer")
		{
			const std::vector<uint32> healerClasses = { CLASS_PRIEST, CLASS_DRUID,CLASS_SHAMAN,CLASS_PALADIN };
			botClass = static_cast<uint8>(SelectRandomContainerElement(healerClasses));
			botRole = ROLE_HEALER;
		}
		else if (option == "tank")
		{
			const std::vector<uint32> tankClasses = { CLASS_WARRIOR, CLASS_DRUID,CLASS_SHAMAN,CLASS_PALADIN };
			botClass = static_cast<uint8>(SelectRandomContainerElement(tankClasses));
			botRole = ROLE_TANK;
		}

		ExtractUInt32(&args, botLevel);
	}
	if (!botClass)
	{
		SendSysMessage("Incorrect syntax. Expected role or class.");
		SetSentErrorMessage(true);
		return false;
	}

	// Handle Role
	if (const char* arg2 = ExtractArg(&args))
	{
		const std::string option = arg2;
		if (option == "tank")
			botRoleOption = ROLE_TANK;
		else if (option == "healer")
			botRoleOption = ROLE_HEALER;
		else if (option == "dps")
			botRoleOption = CombatBotBaseAI::IsMeleeDamageClass(botClass) ? ROLE_MELEE_DPS : ROLE_RANGE_DPS;
		else if (option == "meleedps")
			botRoleOption = ROLE_MELEE_DPS;
		else if (option == "rangedps")
			botRoleOption = ROLE_RANGE_DPS;
	}
	if (botRole == ROLE_INVALID && botRoleOption != ROLE_INVALID)
		botRole = SelectSpecificRoleForClass(botClass, botRoleOption);

	// Handle Race
	if (const char* arg3 = ExtractArg(&args))
	{
		const std::string option = arg3;
		if (option == "human")
			botRace = RACE_HUMAN;
		else if (option == "dwarf")
			botRace = RACE_DWARF;
		else if (option == "nightelf")
			botRace = RACE_NIGHTELF;
		else if (option == "gnome")
			botRace = RACE_GNOME;
		else if (option == "orc")
			botRace = RACE_ORC;
		else if (option == "undead")
			botRace = RACE_UNDEAD;
		else if (option == "tauren")
			botRace = RACE_TAUREN;
		else if (option == "troll")
			botRace = RACE_TROLL;
		else if (option == "draenei")
			botRace = RACE_DRAENEI;
		else if (option == "bloodelf")
			botRace = RACE_BLOODELF;
	}
	if (!botRace)
		botRace = SelectRandomRaceForClass(botClass, pPlayer->GetTeam());
	else
		botRace = SelectSpecificRaceForClass(botClass, pPlayer->GetTeam(), botRace);

	// Handle Gender
	auto botGender = static_cast<uint8>(urand(GENDER_MALE, GENDER_FEMALE));
	if (const char* arg4 = ExtractArg(&args))
	{
		const std::string option = arg4;
		if (option == "male")
			botGender = GENDER_MALE;
		else if (option == "female")
			botGender = GENDER_FEMALE;
	}

	// Handle Position
	float x, y, z;
	pPlayer->GetNearPoint(pPlayer, x, y, z, 0, 5.0f, frand(0.0f, 6.0f));

	const auto ai = new PartyBotAI(pPlayer, nullptr, botRole, botClass, botRace, botGender, botLevel, pPlayer->GetMapId(), pPlayer->GetMap()->GetInstanceId(), x, y, z, pPlayer->GetOrientation());
	if (sPlayerBotMgr.AddBot(ai))
	{
		PSendSysMessage(DO_COLOR(COLOR_WHITE, "New ") DO_COLOR(COLOR_ORANGE, "Companion") DO_COLOR(COLOR_WHITE, " added."));
	}
	else
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "Can't hire this Companion."));
		SetSentErrorMessage(true);
		return false;
	}

	return true;
}

bool ChatHandler::HandlePartyBotRemoveCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	if (!pTarget)
	{
		SetSentErrorMessage(true);
		return false;
	}

	if (pTarget->AI_NYCTERMOON())
	{
		if (const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON()))
		{
			Player* pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return true;
			}

			if (pLeader && pLeader != pPlayer)
			{
				SendSysMessage("You can only dismiss your own Companions.");
				SetSentErrorMessage(true);
				return false;
			}

			pAI->GetBotEntry()->SetRequestRemoval(true);
			return true;
		}
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "Target is not a ") DO_COLOR(COLOR_ORANGE, "Companion."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotCloneCommand(char* args)
{
	Player* pPlayer = m_session->GetPlayer();
	if (!pPlayer)
		return false;

	Player* pTarget = getSelectedPlayer();
	if (!pTarget)
	{
		SetSentErrorMessage(true);
		return false;
	}

	if (!PartyBotAddRequirementCheck(pPlayer, pTarget))
	{
		SetSentErrorMessage(true);
		return false;
	}

	if (Group* pGroup = pPlayer->GetGroup())
	{
		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
				{
					if (pAI->IsClone && pAI->m_leaderGUID == pPlayer->GetObjectGuid())
					{
						SendSysMessage("You may only have one Clone at a time.");
						SetSentErrorMessage(true);
						return false;
					}
				}
			}
		}
	}

	if (!pPlayer->CanAddClone && pPlayer->GetSession()->GetSecurity() == SEC_PLAYER)
	{
		const uint32 duration = pPlayer->AddClone.GetExpiry() / IN_MILLISECONDS;
		const std::string duration_t = secsToTimeString(duration);
		SendSysMessage("The ability to create a Clone is on cooldown.");
		PSendSysMessage("Wait %s seconds before trying again.", duration_t.c_str());
		SetSentErrorMessage(true);
		return false;
	}

	const uint8 botRace = pTarget->getRace();
	const uint8 botClass = pTarget->getClass();
	const uint8 botGender = pTarget->getGender();

	float x, y, z;
	pPlayer->GetNearPoint(pPlayer, x, y, z, 0, 5.0f, frand(0.0f, 6.0f));

	const auto ai = new PartyBotAI(pPlayer, pTarget, ROLE_INVALID, botClass, botRace, botGender, pPlayer->GetLevel(), pPlayer->GetMapId(), pPlayer->GetMap()->GetInstanceId(), x, y, z, pPlayer->GetOrientation());
	if (sPlayerBotMgr.AddBot(ai))
	{
		pPlayer->CanAddClone = false;
		SendSysMessage("New Clone created. It will exist for 5 minutes.");
		SendSysMessage("You must assign it a role!");
	}
	else
	{
		SendSysMessage("Error creating Clone.");
		SetSentErrorMessage(true);
		return false;
	}

	return true;
}

bool ChatHandler::HandlePartyBotSetRoleCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	Group* pGroup = pPlayer->GetGroup();
	if (!pGroup)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
		SetSentErrorMessage(true);
		return false;
	}

	if (strlen(args) == 0)
	{
		SendSysMessage("Valid roles are: tank, healer, dps, mdps, rdps.");
		SetSentErrorMessage(true);
		return false;
	}

	if (!pTarget)
	{
		SetSentErrorMessage(true);
		return false;
	}

	CombatBotRoles role = ROLE_INVALID;
	std::string roleStr = args;

	if (roleStr == "tank")
		role = ROLE_TANK;
	else if (roleStr == "dps")
		role = CombatBotBaseAI::IsMeleeDamageClass(pTarget->getClass()) ? ROLE_MELEE_DPS : ROLE_RANGE_DPS;
	else if (roleStr == "mdps")
		role = ROLE_MELEE_DPS;
	else if (roleStr == "rdps")
		role = ROLE_RANGE_DPS;
	else if (roleStr == "healer")
		role = ROLE_HEALER;

	if (role == ROLE_INVALID)
	{
		SetSentErrorMessage(true);
		return false;
	}

	if (auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON()))
	{
		Player* pLeader = pAI->GetPartyLeader();
		if (!pLeader)
		{
			pAI->GetBotEntry()->SetRequestRemoval(true);
			return false;
		}

		if (pLeader && pLeader != pPlayer)
		{
			SendSysMessage(DO_COLOR(COLOR_WHITE, "You can only set the role of your own ") DO_COLOR(COLOR_ORANGE, "Companions."));
			SetSentErrorMessage(true);
			return false;
		}

		const uint8 MyClass = pTarget->getClass();
		if (MyClass == CLASS_WARRIOR && role != ROLE_TANK && role != ROLE_MELEE_DPS ||
			MyClass == CLASS_PALADIN && role == ROLE_RANGE_DPS ||
			MyClass == CLASS_HUNTER && role != ROLE_RANGE_DPS ||
			MyClass == CLASS_ROGUE && role != ROLE_MELEE_DPS ||
			MyClass == CLASS_PRIEST && role != ROLE_HEALER && role != ROLE_RANGE_DPS ||
			MyClass == CLASS_MAGE && role != ROLE_RANGE_DPS ||
			MyClass == CLASS_WARLOCK && role != ROLE_RANGE_DPS)
		{
			std::string chatMsg = "%s";
			chatMsg += DO_COLOR(COLOR_WHITE, " can't do that role.");
			PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			SetSentErrorMessage(true);
			return false;
		}

		pAI->m_role = role;
		pAI->SetFormationPosition(pLeader);

		if (roleStr == "tank")
			roleStr = "Tank";
		else if (roleStr == "healer")
			roleStr = "Healer";
		else if (roleStr == "mdps")
			roleStr = "Melee DPS";
		else if (roleStr == "rdps")
			roleStr = "Ranged DPS";
		else if (roleStr == "dps")
		{
			if (role == ROLE_MELEE_DPS)
				roleStr = "Melee DPS";
			else
				roleStr = "Ranged DPS";
		}

		std::string chatMsg = "%s";
		chatMsg += DO_COLOR(COLOR_WHITE, " is ");
		chatMsg += DO_COLOR(COLOR_BLUE, "set");
		chatMsg += DO_COLOR(COLOR_WHITE, " to ");
		chatMsg += DO_COLOR(COLOR_GOLD, "%s.");
		PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str(), roleStr.c_str());
		return true;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "Target is not a ") DO_COLOR(COLOR_ORANGE, "Companion."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotComeToggleCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	if (!pPlayer)
		return false;

	if (pTarget && pTarget != pPlayer)
	{
		Player* pLeader = nullptr;
		const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON());
		if (pAI)
		{
			pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return false;
			}
		}

		if (pAI && pLeader && pLeader == pPlayer)
		{
			bool unstaying = false;
			if (!pTarget->HasAuraType(SPELL_AURA_MOD_ROOT) && pAI->IsStaying == true)
			{
				pTarget->clearUnitState(UNIT_STAT_ROOT);
				pAI->IsStaying = false;
				unstaying = true;
			}

			const uint8 ok = HandlePartyBotComeToggleHelper(pTarget, pPlayer);
			if (ok == 1)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " has toggled ");
				chatMsg += DO_COLOR(COLOR_BLUE, "come");
				chatMsg += DO_COLOR(COLOR_WHITE, " [");
				chatMsg += DO_COLOR(COLOR_GREEN, "ON");
				chatMsg += DO_COLOR(COLOR_WHITE, "]");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			else if (ok == 2)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " has toggled ");
				chatMsg += DO_COLOR(COLOR_BLUE, "come");
				chatMsg += DO_COLOR(COLOR_WHITE, " [");
				chatMsg += DO_COLOR(COLOR_RED, "OFF");
				chatMsg += DO_COLOR(COLOR_WHITE, "]");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			else if (unstaying)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is no longer staying.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			else
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is not a valid ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companion");
				chatMsg += DO_COLOR(COLOR_WHITE, " for your command.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			if (ok) return true;
		}
	}

	if (Group* pGroup = pPlayer->GetGroup())
	{
		bool ok = false;
		bool unstaying = false;
		char* arg1 = ExtractArg(&args);
		char* arg2 = ExtractArg(&args);

		auto argClass = argClasses.begin();
		auto argRole = argRoles.begin();

		if (arg1)
		{
			const std::string option = arg1;
			argClass = argClasses.find(option);
			argRole = argRoles.find(option);

			if (argClass == argClasses.end() && argRole == argRoles.end() && option != "dps")
			{
				SendSysMessage("Valid options are: tank, healer, dps, mdps, rdps, warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid.");
				SetSentErrorMessage(true);
				return false;
			}
		}

		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				Player* pLeader = nullptr;

				if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
				{
					pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (arg1)
				{
					if (argClass != argClasses.end())
					{
						if (arg2)
						{
							if (pMember->getClass() == argClass->second)
								continue;
						}
						else if (pMember->getClass() != argClass->second)
							continue;
					}
					else if (argRole != argRoles.end())
					{
						if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
						{
							if (arg2)
							{
								if (pAI->m_role == argRole->second)
									continue;
							}
							else if (pAI->m_role != argRole->second)
								continue;
						}
					}
					else
					{
						const std::string option = arg1;

						if (option == "dps")
						{
							if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
							{
								if (arg2)
								{
									if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
										continue;
								}
								else if (!(pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS))
									continue;
							}
						}
					}
				}

				if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
				{
					if (!pMember->HasAuraType(SPELL_AURA_MOD_ROOT) && pAI->IsStaying)
					{
						pMember->clearUnitState(UNIT_STAT_ROOT);
						pAI->IsStaying = false;
						unstaying = true;
					}
				}

				std::string choice;
				if (argClass != argClasses.end())
					choice = argClass->first;
				else if (argRole != argRoles.end())
					choice = argRole->first;
				else
					choice = "dps";
				const char* c = choice.c_str();

				const uint8 check = HandlePartyBotComeToggleHelper(pMember, pPlayer);
				if (check == 1)
				{
					std::string chatMsg = "%s";
					chatMsg += DO_COLOR(COLOR_WHITE, " has toggled ");
					chatMsg += DO_COLOR(COLOR_BLUE, "come");
					chatMsg += DO_COLOR(COLOR_WHITE, " [");
					chatMsg += DO_COLOR(COLOR_GREEN, "ON");
					chatMsg += DO_COLOR(COLOR_WHITE, "]");
					if (arg1)
					{
						chatMsg += DO_COLOR(COLOR_WHITE, " { ");
						if (arg2)
							chatMsg += DO_COLOR(COLOR_RED, "except ");
						chatMsg += DO_COLOR(COLOR_GOLD, "%s");
						chatMsg += DO_COLOR(COLOR_WHITE, " }");
					}

					if (arg1)
						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str(), c);
					else
						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
				}
				else if (check == 2)
				{
					std::string chatMsg = "%s";
					chatMsg += DO_COLOR(COLOR_WHITE, " has toggled ");
					chatMsg += DO_COLOR(COLOR_BLUE, "come");
					chatMsg += DO_COLOR(COLOR_WHITE, " [");
					chatMsg += DO_COLOR(COLOR_RED, "OFF");
					chatMsg += DO_COLOR(COLOR_WHITE, "]");
					if (arg1)
					{
						chatMsg += DO_COLOR(COLOR_WHITE, " { ");
						if (arg2)
							chatMsg += DO_COLOR(COLOR_RED, "except ");
						chatMsg += DO_COLOR(COLOR_GOLD, "%s");
						chatMsg += DO_COLOR(COLOR_WHITE, " }");
					}

					if (arg1)
						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str(), c);
					else
						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
				}
				else if (unstaying)
				{
					std::string chatMsg = "%s";
					chatMsg += DO_COLOR(COLOR_WHITE, " is no longer staying.");
					PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
				}
				else continue;
				if (check) ok = true;
			}
		}

		if (!ok) SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
		return ok;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotComeAndStayCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	if (!pPlayer)
		return false;

	if (pTarget && pTarget != pPlayer)
	{
		Player* pLeader = nullptr;
		const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON());
		if (pAI)
		{
			pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return false;
			}
		}

		if (pLeader && pLeader == pPlayer)
		{
			if (pAI && pAI->m_toggle_come)
			{
				if (!pAI->IsStaying && pTarget->hasUnitState(UNIT_STAT_ROOT))
					pTarget->clearUnitState(UNIT_STAT_ROOT);
				pAI->m_toggle_come = false;

				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " has toggled ");
				chatMsg += DO_COLOR(COLOR_BLUE, "come");
				chatMsg += DO_COLOR(COLOR_WHITE, " [");
				chatMsg += DO_COLOR(COLOR_RED, "OFF");
				chatMsg += DO_COLOR(COLOR_WHITE, "]");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}

			const bool ok = HandlePartyBotComeAndStayHelper(pTarget, pPlayer);
			if (ok)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " will ");
				chatMsg += DO_COLOR(COLOR_BLUE, "come and stay");
				chatMsg += DO_COLOR(COLOR_WHITE, " at your position.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			else
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is not a valid ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companion");
				chatMsg += DO_COLOR(COLOR_WHITE, " for your command.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			return ok;
		}
	}

	if (Group* pGroup = pPlayer->GetGroup())
	{
		bool ok = false;
		char* arg1 = ExtractArg(&args);
		char* arg2 = ExtractArg(&args);

		auto argClass = argClasses.begin();
		auto argRole = argRoles.begin();

		if (arg1)
		{
			const std::string option = arg1;
			argClass = argClasses.find(option);
			argRole = argRoles.find(option);

			if (argClass == argClasses.end() && argRole == argRoles.end() && option != "dps")
			{
				SendSysMessage("Valid options are: tank, healer, dps, mdps, rdps, warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid.");
				SetSentErrorMessage(true);
				return false;
			}
		}

		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				Player* pLeader = nullptr;
				const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
				if (!pAI) continue;
				if (pAI)
				{
					pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (arg1)
				{
					if (argClass != argClasses.end())
					{
						if (arg2)
						{
							if (pMember->getClass() == argClass->second)
								continue;
						}
						else if (pMember->getClass() != argClass->second)
							continue;
					}
					else if (argRole != argRoles.end())
					{
						if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
						{
							if (arg2)
							{
								if (pAI->m_role == argRole->second)
									continue;
							}
							else if (pAI->m_role != argRole->second)
								continue;
						}
					}
					else
					{
						const std::string option = arg1;
						if (option == "dps")
						{
							if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
							{
								if (arg2)
								{
									if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
										continue;
								}
								else if (!(pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS))
									continue;
							}
						}
					}
				}

				if (pAI && pAI->m_toggle_come)
				{
					std::string choice;
					if (argClass != argClasses.end())
						choice = argClass->first;
					else if (argRole != argRoles.end())
						choice = argRole->first;
					else
						choice = "dps";
					const char* c = choice.c_str();

					if (!pAI->IsStaying && pMember->hasUnitState(UNIT_STAT_ROOT))
						pMember->clearUnitState(UNIT_STAT_ROOT);
					pAI->m_toggle_come = false;

					std::string chatMsg = "%s";
					chatMsg += DO_COLOR(COLOR_WHITE, " has toggled ");
					chatMsg += DO_COLOR(COLOR_BLUE, "come");
					chatMsg += DO_COLOR(COLOR_WHITE, " [");
					chatMsg += DO_COLOR(COLOR_RED, "OFF");
					chatMsg += DO_COLOR(COLOR_WHITE, "]");
					if (arg1)
					{
						chatMsg += DO_COLOR(COLOR_WHITE, " { ");
						if (arg2)
							chatMsg += DO_COLOR(COLOR_RED, "except ");
						chatMsg += DO_COLOR(COLOR_GOLD, "%s");
						chatMsg += DO_COLOR(COLOR_WHITE, " }");
					}

					if (arg1)
						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str(), c);
					else
						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
				}

				if (HandlePartyBotComeAndStayHelper(pMember, pPlayer))
					ok = true;
			}
		}

		if (ok)
		{
			if (arg1)
			{
				std::string choice;
				if (argClass != argClasses.end())
					choice = argClass->first;
				else if (argRole != argRoles.end())
					choice = argRole->first;
				else
					choice = "dps";
				const char* c = choice.c_str();

				if (arg2)
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " {");
					chatMsg += DO_COLOR(COLOR_RED, " except ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } will ");
					chatMsg += DO_COLOR(COLOR_BLUE, "come and stay");
					chatMsg += DO_COLOR(COLOR_WHITE, " at your position.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
				else
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " will ");
					chatMsg += DO_COLOR(COLOR_BLUE, "come and stay");
					chatMsg += DO_COLOR(COLOR_WHITE, " at your position.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
			}
			else
			{
				std::string chatMsg;
				chatMsg += DO_COLOR(COLOR_WHITE, "All ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
				chatMsg += DO_COLOR(COLOR_WHITE, " will ");
				chatMsg += DO_COLOR(COLOR_BLUE, "come and stay");
				chatMsg += DO_COLOR(COLOR_WHITE, " at your position.");
				PSendSysMessage(chatMsg.c_str());
			}
		}
		else
			SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
		return ok;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotComeToMeCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	if (!pPlayer)
		return false;

	if (pTarget && pTarget != pPlayer)
	{
		Player* pLeader = nullptr;
		const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON());
		if (pAI)
		{
			pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return false;
			}
		}

		if (pAI && pLeader && pLeader == pPlayer)
		{
			bool unstaying = false;
			if (!pTarget->HasAuraType(SPELL_AURA_MOD_ROOT) && pAI->IsStaying == true)
			{
				pTarget->clearUnitState(UNIT_STAT_ROOT);
				pAI->IsStaying = false;
				unstaying = true;
			}

			if (pAI && pAI->m_toggle_come)
			{
				if (!pAI->IsStaying && pTarget->hasUnitState(UNIT_STAT_ROOT))
					pTarget->clearUnitState(UNIT_STAT_ROOT);
				pAI->m_toggle_come = false;

				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " has toggled ");
				chatMsg += DO_COLOR(COLOR_BLUE, "come");
				chatMsg += DO_COLOR(COLOR_WHITE, " [");
				chatMsg += DO_COLOR(COLOR_RED, "OFF");
				chatMsg += DO_COLOR(COLOR_WHITE, "]");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}

			const bool ok = HandlePartyBotComeToMeHelper(pTarget, pPlayer);
			if (ok)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " will ");
				chatMsg += DO_COLOR(COLOR_BLUE, "come");
				chatMsg += DO_COLOR(COLOR_WHITE, " at your position.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			else if (unstaying)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is no longer on ");
				chatMsg += DO_COLOR(COLOR_BLUE, "stay.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			else
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is not a valid ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companion");
				chatMsg += DO_COLOR(COLOR_WHITE, " for your command.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			return ok;
		}
	}

	if (Group* pGroup = pPlayer->GetGroup())
	{
		bool ok = false;
		bool unstaying = false;
		char* arg1 = ExtractArg(&args);
		char* arg2 = ExtractArg(&args);

		auto argClass = argClasses.begin();
		auto argRole = argRoles.begin();

		if (arg1)
		{
			const std::string option = arg1;
			argClass = argClasses.find(option);
			argRole = argRoles.find(option);

			if (argClass == argClasses.end() && argRole == argRoles.end() && option != "dps")
			{
				SendSysMessage("Valid options are: tank, healer, dps, mdps, rdps, warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid.");
				SetSentErrorMessage(true);
				return false;
			}
		}

		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				Player* pLeader = nullptr;
				const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
				if (!pAI) continue;
				if (pAI)
				{
					pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (arg1)
				{
					if (argClass != argClasses.end())
					{
						if (arg2)
						{
							if (pMember->getClass() == argClass->second)
								continue;
						}
						else if (pMember->getClass() != argClass->second)
							continue;
					}
					else if (argRole != argRoles.end())
					{
						if (pAI)
						{
							if (arg2)
							{
								if (pAI->m_role == argRole->second)
									continue;
							}
							else if (pAI->m_role != argRole->second)
								continue;
						}
					}
					else
					{
						const std::string option = arg1;

						if (option == "dps")
						{
							if (pAI)
							{
								if (arg2)
								{
									if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
										continue;
								}
								else if (!(pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS))
									continue;
							}
						}
					}
				}

				if (!pMember->HasAuraType(SPELL_AURA_MOD_ROOT) && pAI->IsStaying == true)
				{
					if (pMember->hasUnitState(UNIT_STAT_ROOT))
						pMember->clearUnitState(UNIT_STAT_ROOT);
					pAI->IsStaying = false;
					unstaying = true;
				}

				if (pAI && pAI->m_toggle_come)
				{
					std::string choice;
					if (argClass != argClasses.end())
						choice = argClass->first;
					else if (argRole != argRoles.end())
						choice = argRole->first;
					else
						choice = "dps";
					const char* c = choice.c_str();

					if (!pAI->IsStaying && pMember->hasUnitState(UNIT_STAT_ROOT))
						pMember->clearUnitState(UNIT_STAT_ROOT);
					pAI->m_toggle_come = false;

					std::string chatMsg = "%s";
					chatMsg += DO_COLOR(COLOR_WHITE, " has toggled ");
					chatMsg += DO_COLOR(COLOR_BLUE, "come");
					chatMsg += DO_COLOR(COLOR_WHITE, " [");
					chatMsg += DO_COLOR(COLOR_RED, "OFF");
					chatMsg += DO_COLOR(COLOR_WHITE, "]");
					if (arg1)
					{
						chatMsg += DO_COLOR(COLOR_WHITE, " { ");
						if (arg2)
							chatMsg += DO_COLOR(COLOR_RED, "except ");
						chatMsg += DO_COLOR(COLOR_GOLD, "%s");
						chatMsg += DO_COLOR(COLOR_WHITE, " }");
					}

					if (arg1)
						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str(), c);
					else
						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
				}

				if (HandlePartyBotComeToMeHelper(pMember, pPlayer))
					ok = true;
			}
		}

		if (ok)
		{
			if (arg1)
			{
				std::string choice;
				if (argClass != argClasses.end())
					choice = argClass->first;
				else if (argRole != argRoles.end())
					choice = argRole->first;
				else
					choice = "dps";
				const char* c = choice.c_str();

				if (arg2)
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " {");
					chatMsg += DO_COLOR(COLOR_RED, " except ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } will ");
					chatMsg += DO_COLOR(COLOR_BLUE, "come");
					chatMsg += DO_COLOR(COLOR_WHITE, " at your position.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
				else
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " will ");
					chatMsg += DO_COLOR(COLOR_BLUE, "come");
					chatMsg += DO_COLOR(COLOR_WHITE, " at your position.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
			}
			else
			{
				std::string chatMsg;
				chatMsg += DO_COLOR(COLOR_WHITE, "All ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
				chatMsg += DO_COLOR(COLOR_WHITE, " will ");
				chatMsg += DO_COLOR(COLOR_BLUE, "come");
				chatMsg += DO_COLOR(COLOR_WHITE, " at your position.");
				PSendSysMessage(chatMsg.c_str());
			}
		}
		else if (unstaying)
		{
			std::string chatMsg;
			if (arg1)
			{
				std::string choice;
				if (argClass != argClasses.end())
					choice = argClass->first;
				else if (argRole != argRoles.end())
					choice = argRole->first;
				else
					choice = "dps";
				const char* c = choice.c_str();

				if (arg2)
				{
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " {");
					chatMsg += DO_COLOR(COLOR_RED, " except ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } are no longer on ");
					chatMsg += DO_COLOR(COLOR_BLUE, "stay.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
				else if (arg1)
				{
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " are no longer on ");
					chatMsg += DO_COLOR(COLOR_BLUE, "stay.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
			}
			else
			{
				chatMsg += DO_COLOR(COLOR_WHITE, "All ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
				chatMsg += DO_COLOR(COLOR_WHITE, " are no longer on ");
				chatMsg += DO_COLOR(COLOR_BLUE, "stay.");
				PSendSysMessage(chatMsg.c_str());
			}
		}
		else
			SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
		return ok;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotUseGObjectCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	Group* pGroup = pPlayer->GetGroup();
	if (!pGroup)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
		SetSentErrorMessage(true);
		return false;
	}

	char text[1] = {};
	ChatHandler(pPlayer).HandleGameObjectSelectCommand(text); // TODO FIX
	GameObject* pGo = pPlayer->GetMap()->GetGameObject(pPlayer->GetSelectionGuid());

	// If the selected object is too far away then deselect it
	if (pGo && pGo->GetDistance(pPlayer) > 3.0f)
		pGo = nullptr;

	// If no object is selected then send error message
	if (!pGo)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "You must to be close to a valid object to ") DO_COLOR(COLOR_BLUE, "use") DO_COLOR(COLOR_WHITE, " it."));
		SetSentErrorMessage(true);
		return false;
	}

	uint8 ok = false;
	if (pTarget && pTarget != pPlayer)
	{
		Player* pLeader = nullptr;
		if (const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON()))
		{
			if (pAI->m_DND)
				return false;

			pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return false;
			}
		}

		if (pLeader && pLeader == pPlayer)
		{
			ok = HandlePartyBotUseGObjectHelper(pTarget, pGo, pPlayer);
			if (ok == 4)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " doesn't know how to Lockpick.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
				return false;
			}
			if (ok == 3)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " has ");
				chatMsg += DO_COLOR(COLOR_BLUE, "used ");
				chatMsg += DO_COLOR(COLOR_WHITE, "[");
				chatMsg += DO_COLOR(COLOR_GOLD, "%s");
				chatMsg += DO_COLOR(COLOR_WHITE, "]");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str(), pGo->GetName());
				return true;
			}
			if (ok == 2)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is not a Rogue.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str(), pGo->GetName());
				return false;
			}
			if (ok == 1)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is too far away from ");
				chatMsg += DO_COLOR(COLOR_WHITE, "[");
				chatMsg += DO_COLOR(COLOR_GOLD, "%s");
				chatMsg += DO_COLOR(COLOR_WHITE, "]");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str(), pGo->GetName());
				return false;
			}
			if (!ok)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " can't do this.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
				return false;
			}
		}
	}

	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (Player* pMember = itr->getSource())
		{
			if (pMember == pPlayer)
				continue;

			if (!pMember->AI_NYCTERMOON())
				continue;

			if (pMember->IsTaxiFlying())
				continue;

			Player* pLeader = nullptr;
			if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
			{
				pLeader = pAI->GetPartyLeader();
				if (!pLeader)
				{
					pAI->GetBotEntry()->SetRequestRemoval(true);
					return false;
				}
			}

			if (pLeader && pLeader != pPlayer)
				continue;

			ok = HandlePartyBotUseGObjectHelper(pMember, pGo, pPlayer);
			if (ok == 4)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " doesn't know how to Lockpick.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
			}
			else if (ok == 3)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " has ");
				chatMsg += DO_COLOR(COLOR_BLUE, "used ");
				chatMsg += DO_COLOR(COLOR_WHITE, "[");
				chatMsg += DO_COLOR(COLOR_GOLD, "%s");
				chatMsg += DO_COLOR(COLOR_WHITE, "]");
				PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str(), pGo->GetName());
			}
			else if (ok == 2)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is not a Rogue.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str(), pGo->GetName());
			}
			else if (ok == 1)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is too far away from ");
				chatMsg += DO_COLOR(COLOR_WHITE, "[");
				chatMsg += DO_COLOR(COLOR_GOLD, "%s");
				chatMsg += DO_COLOR(COLOR_WHITE, "]");
				PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str(), pGo->GetName());
			}
			else if (!ok)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " can't do this.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
			}
		}
	}
	if (ok)	return true;

	return false;
}

bool ChatHandler::HandlePartyBotTankPullCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Unit* pVictim = getSelectedUnit();

	Group* pGroup = pPlayer->GetGroup();
	if (!pGroup)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
		SetSentErrorMessage(true);
		return false;
	}

	bool unfrozen = false;
	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (Player* pMember = itr->getSource())
		{
			if (pMember == pPlayer)
				continue;

			if (!pMember->AI_NYCTERMOON())
				continue;

			Player* pLeader = nullptr;
			const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
			if (!pAI) continue;
			if (pAI)
			{
				pLeader = pAI->GetPartyLeader();
				if (!pLeader)
				{
					pAI->GetBotEntry()->SetRequestRemoval(true);
					return false;
				}
			}

			if (pLeader && pLeader != pPlayer)
				continue;

			// Remove statuses
			if (pAI->IsStaying && !pMember->HasAuraType(SPELL_AURA_MOD_ROOT))
			{
				if (pMember->hasUnitState(UNIT_STAT_ROOT))
					pMember->clearUnitState(UNIT_STAT_ROOT);
				pAI->IsStaying = false;
				unfrozen = true;
			}

			if (pAI->IsPassive == true)
			{
				pAI->IsPassive = false;
				unfrozen = true;
			}

			if (pAI && pAI->m_toggle_come)
			{
				if (!pAI->IsStaying && pMember->hasUnitState(UNIT_STAT_ROOT))
					pMember->clearUnitState(UNIT_STAT_ROOT);
				pAI->m_toggle_come = false;
				unfrozen = true;

				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " has toggled ");
				chatMsg += DO_COLOR(COLOR_BLUE, "come");
				chatMsg += DO_COLOR(COLOR_WHITE, " [");
				chatMsg += DO_COLOR(COLOR_RED, "OFF");
				chatMsg += DO_COLOR(COLOR_WHITE, "]");
				PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
			}

			if (pMember->getStandState() != UNIT_STAND_STATE_STAND)
				pMember->SetStandState(UNIT_STAND_STATE_STAND);
		}
	}
	if (unfrozen)
	{
		std::string chatMsg;
		chatMsg += DO_COLOR(COLOR_WHITE, "All ");
		chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
		chatMsg += DO_COLOR(COLOR_WHITE, " are ");
		chatMsg += DO_COLOR(COLOR_BLUE, "active");
		chatMsg += DO_COLOR(COLOR_WHITE, " again.");
		PSendSysMessage(chatMsg.c_str());
	}

	// Check if an enemy was selected
	if (!pVictim || pVictim == pPlayer)
	{
		std::string chatMsg;
		chatMsg += DO_COLOR(COLOR_WHITE, "You should select a ");
		chatMsg += DO_COLOR(COLOR_RED, "hostile");
		chatMsg += DO_COLOR(COLOR_WHITE, " target.");
		SendSysMessage(chatMsg.c_str());
		SetSentErrorMessage(true);
		return false;
	}

	uint32 duration = 0;
	if (char* arg1 = ExtractArg(&args))
	{
		if (!duration)
			duration = atoi(arg1) * IN_MILLISECONDS;
	}
	//if no argument was passed then set pause duration to 10 seconds by default
	if (duration <= 0) duration = 10 * IN_MILLISECONDS;

	bool dps_paused = false;
	bool tank_attacking = false;

	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (Player* pMember = itr->getSource())
		{
			if (pMember == pPlayer)
				continue;

			if (pMember->IsTaxiFlying())
				continue;

			if (pMember->AI_NYCTERMOON())
			{
				if (auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
				{
					Player* pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}

					if (pLeader && pLeader != pPlayer)
						continue;

					if (!pMember->HasAuraType(SPELL_AURA_MOD_ROOT) && pAI->IsStaying == true)
					{
						pMember->clearUnitState(UNIT_STAT_ROOT);
						pAI->IsStaying = false;
					}

					if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
					{
						if (pMember->getClass() == CLASS_HUNTER &&
							pMember->GetLevel() >= 40 &&
							pMember->HasAura(13159))
							pMember->RemoveAurasDueToSpellByCancel(13159);

						if (HandlePartyBotPauseApplyHelper(pMember, duration))
							dps_paused = true;
					}
					else if (pAI->m_role == ROLE_TANK)
					{
						if (!pVictim || pVictim == pPlayer)
							continue;

						if (pMember->IsValidAttackTarget(pVictim) && !pMember->IsTaxiFlying())
						{
							pAI->AttackStart(pVictim);
							tank_attacking = true;
						}
					}
				}
			}
		}
	}

	if (tank_attacking && dps_paused)
	{
		std::string chatMsg;
		chatMsg += DO_COLOR(COLOR_WHITE, "All ");
		chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
		chatMsg += DO_COLOR(COLOR_GOLD, "tank");
		chatMsg += DO_COLOR(COLOR_WHITE, " } ");
		chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
		chatMsg += DO_COLOR(COLOR_WHITE, " will ");
		chatMsg += DO_COLOR(COLOR_BLUE, "attack ");
		chatMsg += "%s";
		PSendSysMessage(chatMsg.c_str(), playerLink(pVictim->GetName()).c_str());

		chatMsg = DO_COLOR(COLOR_WHITE, "All ");
		chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
		chatMsg += DO_COLOR(COLOR_GOLD, "dps");
		chatMsg += DO_COLOR(COLOR_WHITE, " } ");
		chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
		chatMsg += DO_COLOR(COLOR_WHITE, " are ");
		chatMsg += DO_COLOR(COLOR_BLUE, "paused");
		chatMsg += DO_COLOR(COLOR_WHITE, " for ");
		chatMsg += DO_COLOR(COLOR_BLUE, "%s.");
		PSendSysMessage(chatMsg.c_str(), secsToTimeString(duration / IN_MILLISECONDS).c_str());
	}
	else if (tank_attacking && !dps_paused)
	{
		std::string chatMsg;
		chatMsg += DO_COLOR(COLOR_WHITE, "All ");
		chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
		chatMsg += DO_COLOR(COLOR_GOLD, "tank");
		chatMsg += DO_COLOR(COLOR_WHITE, " } ");
		chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
		chatMsg += DO_COLOR(COLOR_WHITE, " will ");
		chatMsg += DO_COLOR(COLOR_BLUE, "attack ");
		chatMsg += "%s";
		PSendSysMessage(chatMsg.c_str(), playerLink(pVictim->GetName()).c_str());
	}
	else if (!tank_attacking && dps_paused)
	{
		std::string chatMsg;
		chatMsg += DO_COLOR(COLOR_WHITE, "All ");
		chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
		chatMsg += DO_COLOR(COLOR_GOLD, "dps");
		chatMsg += DO_COLOR(COLOR_WHITE, " } ");
		chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
		chatMsg += DO_COLOR(COLOR_WHITE, " are ");
		chatMsg += DO_COLOR(COLOR_BLUE, "paused");
		chatMsg += DO_COLOR(COLOR_WHITE, " for ");
		chatMsg += DO_COLOR(COLOR_BLUE, "%s.");
		PSendSysMessage(chatMsg.c_str(), secsToTimeString(duration / IN_MILLISECONDS).c_str());
	}
	else if (!tank_attacking && !dps_paused && !unfrozen)
		PSendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
	return true;
}

bool ChatHandler::HandlePartyBotPauseCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();
	uint32 duration = 5 * MINUTE * IN_MILLISECONDS;

	if (!pPlayer)
		return false;

	if (pTarget && pTarget != pPlayer)
	{
		Player* pLeader = nullptr;
		const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON());
		if (pAI)
		{
			pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return false;
			}
		}

		if (char* arg1 = ExtractArg(&args))
		{
			duration = atoi(arg1) * IN_MILLISECONDS;
			if (duration < sPlayerBotMgr.BOT_UPDATE_INTERVAL)
				duration = 5 * MINUTE * IN_MILLISECONDS;
		}

		if (pLeader && pLeader == pPlayer)
		{
			const bool ok = HandlePartyBotPauseApplyHelper(pTarget, duration);
			if (ok)
			{
				if (duration)
				{
					std::string chatMsg = "%s";
					chatMsg += DO_COLOR(COLOR_WHITE, " is ");
					chatMsg += DO_COLOR(COLOR_BLUE, "paused");
					chatMsg += DO_COLOR(COLOR_WHITE, " for ");
					chatMsg += DO_COLOR(COLOR_BLUE, "%s.");
					PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str(), secsToTimeString(duration / IN_MILLISECONDS).c_str());
				}
				else
				{
					std::string chatMsg = "%s";
					chatMsg += DO_COLOR(COLOR_WHITE, " is ");
					chatMsg += DO_COLOR(COLOR_BLUE, "unpaused.");
					PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
				}
			}
			else
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is not a valid ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companion");
				chatMsg += DO_COLOR(COLOR_WHITE, " for your command.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			return ok;
		}
	}

	if (Group* pGroup = pPlayer->GetGroup())
	{
		auto argClass = argClasses.begin();
		auto argRole = argRoles.begin();
		char* arg2 = nullptr;
		char* arg3 = nullptr;
		bool ok = false;
		if (char* arg1 = ExtractArg(&args))
		{
			duration = atoi(arg1) * IN_MILLISECONDS;
			if (duration == 0)
			{
				const std::string option = arg1;
				argClass = argClasses.find(option);
				argRole = argRoles.find(option);

				if (!(argClass == argClasses.end() && argRole == argRoles.end() && option != "dps"))
				{
					arg2 = arg1;
					arg3 = ExtractArg(&args);
				}
			}
			if (duration < sPlayerBotMgr.BOT_UPDATE_INTERVAL)
				duration = 5 * MINUTE * IN_MILLISECONDS;
		}

		if (!arg2)
			arg2 = ExtractArg(&args);
		if (!arg3)
			arg3 = ExtractArg(&args);

		if (arg2)
		{
			const std::string option = arg2;
			argClass = argClasses.find(option);
			argRole = argRoles.find(option);

			if (argClass == argClasses.end() && argRole == argRoles.end() && option != "dps")
			{
				SendSysMessage("Valid options are: tank, healer, dps, mdps, rdps, warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid.");
				SetSentErrorMessage(true);
				return false;
			}
		}

		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				Player* pLeader = nullptr;
				const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
				if (!pAI) continue;
				if (pAI)
				{
					pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (arg2)
				{
					if (argClass != argClasses.end())
					{
						if (arg3)
						{
							if (pMember->getClass() == argClass->second)
								continue;
						}
						else if (pMember->getClass() != argClass->second)
							continue;
					}
					else if (argRole != argRoles.end())
					{
						if (pAI)
						{
							if (arg3)
							{
								if (pAI->m_role == argRole->second)
									continue;
							}
							else if (pAI->m_role != argRole->second)
								continue;
						}
					}
					else
					{
						const std::string option = arg2;

						if (option == "dps")
						{
							if (pAI)
							{
								if (arg3)
								{
									if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
										continue;
								}
								else if (!(pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS))
									continue;
							}
						}
					}
				}

				if (HandlePartyBotPauseApplyHelper(pMember, duration))
					ok = true;
			}
		}

		if (ok)
		{
			if (arg2)
			{
				std::string choice;
				if (argClass != argClasses.end())
					choice = argClass->first;
				else if (argRole != argRoles.end())
					choice = argRole->first;
				else
					choice = "dps";
				const char* c = choice.c_str();

				if (arg3)
				{
					if (duration > sPlayerBotMgr.BOT_UPDATE_INTERVAL)
					{
						std::string chatMsg;
						chatMsg += DO_COLOR(COLOR_WHITE, "All ");
						chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
						chatMsg += DO_COLOR(COLOR_WHITE, " {");
						chatMsg += DO_COLOR(COLOR_RED, " except ");
						chatMsg += DO_COLOR(COLOR_GOLD, "%s");
						chatMsg += DO_COLOR(COLOR_WHITE, " } are ");
						chatMsg += DO_COLOR(COLOR_BLUE, "paused");
						chatMsg += DO_COLOR(COLOR_WHITE, " for ");
						chatMsg += DO_COLOR(COLOR_BLUE, "%s.");
						PSendSysMessage(chatMsg.c_str(), c, secsToTimeString(duration / IN_MILLISECONDS).c_str());
					}
					else
					{
						std::string chatMsg;
						chatMsg += DO_COLOR(COLOR_WHITE, "All ");
						chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
						chatMsg += DO_COLOR(COLOR_WHITE, " {");
						chatMsg += DO_COLOR(COLOR_RED, " except ");
						chatMsg += DO_COLOR(COLOR_GOLD, "%s");
						chatMsg += DO_COLOR(COLOR_WHITE, " } are ");
						chatMsg += DO_COLOR(COLOR_BLUE, "unpaused.");
						PSendSysMessage(chatMsg.c_str(), c);
					}
				}
				else
				{
					if (duration > sPlayerBotMgr.BOT_UPDATE_INTERVAL)
					{
						std::string chatMsg;
						chatMsg += DO_COLOR(COLOR_WHITE, "All ");
						chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
						chatMsg += DO_COLOR(COLOR_GOLD, "%s");
						chatMsg += DO_COLOR(COLOR_WHITE, " } ");
						chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
						chatMsg += DO_COLOR(COLOR_WHITE, " are ");
						chatMsg += DO_COLOR(COLOR_BLUE, "paused");
						chatMsg += DO_COLOR(COLOR_WHITE, " for ");
						chatMsg += DO_COLOR(COLOR_BLUE, "%s.");
						PSendSysMessage(chatMsg.c_str(), c, secsToTimeString(duration / IN_MILLISECONDS).c_str());
					}
					else
					{
						std::string chatMsg;
						chatMsg += DO_COLOR(COLOR_WHITE, "All ");
						chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
						chatMsg += DO_COLOR(COLOR_GOLD, "%s");
						chatMsg += DO_COLOR(COLOR_WHITE, " } ");
						chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
						chatMsg += DO_COLOR(COLOR_WHITE, " are ");
						chatMsg += DO_COLOR(COLOR_BLUE, "unpaused.");
						PSendSysMessage(chatMsg.c_str(), c);
					}
				}
			}
			else
			{
				if (duration > sPlayerBotMgr.BOT_UPDATE_INTERVAL)
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " are ");
					chatMsg += DO_COLOR(COLOR_BLUE, "paused");
					chatMsg += DO_COLOR(COLOR_WHITE, " for ");
					chatMsg += DO_COLOR(COLOR_BLUE, "%s.");
					PSendSysMessage(chatMsg.c_str(), secsToTimeString(duration / IN_MILLISECONDS).c_str());
				}
				else
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " are ");
					chatMsg += DO_COLOR(COLOR_BLUE, "unpaused.");
					PSendSysMessage(chatMsg.c_str());
				}
			}
		}
		else
			SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
		return ok;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotUnpauseCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();
	uint32 duration = sPlayerBotMgr.BOT_UPDATE_INTERVAL;

	if (!pPlayer)
		return false;

	if (pTarget && pTarget != pPlayer)
	{
		Player* pLeader = nullptr;
		const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON());
		if (pAI)
		{
			pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return false;
			}
		}

		if (pLeader && pLeader == pPlayer)
		{
			const bool ok = HandlePartyBotPauseApplyHelper(pTarget, duration);
			if (ok)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is ");
				chatMsg += DO_COLOR(COLOR_BLUE, "unpaused.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			else
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is not a valid ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companion");
				chatMsg += DO_COLOR(COLOR_WHITE, " for your command.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			return ok;
		}
	}

	if (Group* pGroup = pPlayer->GetGroup())
	{
		auto argClass = argClasses.begin();
		auto argRole = argRoles.begin();
		char* arg2 = ExtractArg(&args);
		char* arg3 = ExtractArg(&args);
		bool ok = false;

		if (arg2)
		{
			const std::string option = arg2;
			argClass = argClasses.find(option);
			argRole = argRoles.find(option);

			if (argClass == argClasses.end() && argRole == argRoles.end() && option != "dps")
			{
				SendSysMessage("Valid options are: tank, healer, dps, mdps, rdps, warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid.");
				SetSentErrorMessage(true);
				return false;
			}
		}

		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				Player* pLeader = nullptr;
				const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
				if (!pAI) continue;
				if (pAI)
				{
					pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (arg2)
				{
					if (argClass != argClasses.end())
					{
						if (arg3)
						{
							if (pMember->getClass() == argClass->second)
								continue;
						}
						else if (pMember->getClass() != argClass->second)
							continue;
					}
					else if (argRole != argRoles.end())
					{
						if (pAI)
						{
							if (arg3)
							{
								if (pAI->m_role == argRole->second)
									continue;
							}
							else if (pAI->m_role != argRole->second)
								continue;
						}
					}
					else
					{
						const std::string option = arg2;

						if (option == "dps")
						{
							if (pAI)
							{
								if (arg3)
								{
									if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
										continue;
								}
								else if (!(pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS))
									continue;
							}
						}
					}
				}

				if (HandlePartyBotPauseApplyHelper(pMember, duration))
					ok = true;
			}
		}

		if (ok)
		{
			if (arg2)
			{
				std::string choice;
				if (argClass != argClasses.end())
					choice = argClass->first;
				else if (argRole != argRoles.end())
					choice = argRole->first;
				else
					choice = "dps";
				const char* c = choice.c_str();

				if (arg3)
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " {");
					chatMsg += DO_COLOR(COLOR_RED, " except ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } are ");
					chatMsg += DO_COLOR(COLOR_BLUE, "unpaused.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
				else
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " are ");
					chatMsg += DO_COLOR(COLOR_BLUE, "unpaused.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
			}
			else
			{
				std::string chatMsg;
				chatMsg += DO_COLOR(COLOR_WHITE, "All ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
				chatMsg += DO_COLOR(COLOR_WHITE, " are ");
				chatMsg += DO_COLOR(COLOR_BLUE, "unpaused.");
				PSendSysMessage(chatMsg.c_str());
			}
		}
		else
			SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
		return ok;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotAttackStartCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Unit* pVictim = getSelectedUnit();

	Group* pGroup = pPlayer->GetGroup();
	if (!pGroup)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
		SetSentErrorMessage(true);
		return false;
	}

	// Check arguments
	char* arg1 = ExtractArg(&args);
	char* arg2 = ExtractArg(&args);

	auto argClass = argClasses.begin();
	auto argRole = argRoles.begin();

	if (arg1)
	{
		const std::string option = arg1;
		argClass = argClasses.find(option);
		argRole = argRoles.find(option);

		if (argClass == argClasses.end() && argRole == argRoles.end() && option != "dps")
		{
			SendSysMessage("Valid options are: tank, healer, dps, mdps, rdps, warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid.");
			SetSentErrorMessage(true);
			return false;
		}
	}

	// Remove statuses like stay, cometoggle or paused
	if (pGroup)
	{
		bool unfrozen = false;
		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				Player* pLeader = nullptr;
				const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
				if (!pAI) continue;
				if (pAI)
				{
					pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (arg1)
				{
					if (argClass != argClasses.end())
					{
						if (arg2)
						{
							if (pMember->getClass() == argClass->second)
								continue;
						}
						else if (pMember->getClass() != argClass->second)
							continue;
					}
					else if (argRole != argRoles.end())
					{
						if (pAI)
						{
							if (arg2)
							{
								if (pAI->m_role == argRole->second)
									continue;
							}
							else if (pAI->m_role != argRole->second)
								continue;
						}
					}
					else
					{
						const std::string option = arg1;

						if (option == "dps")
						{
							if (pAI)
							{
								if (arg2)
								{
									if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
										continue;
								}
								else if (!(pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS))
									continue;
							}
						}
					}
				}

				// Remove statuses
				if (pAI->IsStaying && !pMember->HasAuraType(SPELL_AURA_MOD_ROOT))
				{
					if (pMember->hasUnitState(UNIT_STAT_ROOT))
						pMember->clearUnitState(UNIT_STAT_ROOT);
					pAI->IsStaying = false;
					unfrozen = true;
				}

				if (pAI->IsPassive == true)
				{
					pAI->IsPassive = false;
					unfrozen = true;
				}

				if (pAI && pAI->m_toggle_come)
				{
					std::string choice;
					if (argClass != argClasses.end())
						choice = argClass->first;
					else if (argRole != argRoles.end())
						choice = argRole->first;
					else
						choice = "dps";
					const char* c = choice.c_str();

					if (!pAI->IsStaying && pMember->hasUnitState(UNIT_STAT_ROOT))
						pMember->clearUnitState(UNIT_STAT_ROOT);
					pAI->m_toggle_come = false;
					unfrozen = true;

					std::string chatMsg = "%s";
					chatMsg += DO_COLOR(COLOR_WHITE, " has toggled ");
					chatMsg += DO_COLOR(COLOR_BLUE, "come");
					chatMsg += DO_COLOR(COLOR_WHITE, " [");
					chatMsg += DO_COLOR(COLOR_RED, "OFF");
					chatMsg += DO_COLOR(COLOR_WHITE, "]");
					if (arg1)
					{
						chatMsg += DO_COLOR(COLOR_WHITE, " { ");
						if (arg2)
							chatMsg += DO_COLOR(COLOR_RED, "except ");
						chatMsg += DO_COLOR(COLOR_GOLD, "%s");
						chatMsg += DO_COLOR(COLOR_WHITE, " }");
					}

					if (arg1)
						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str(), c);
					else
						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
				}

				if (pAI && pAI->m_updateTimer.GetExpiry() > sPlayerBotMgr.BOT_UPDATE_INTERVAL)
				{
					HandlePartyBotPauseApplyHelper(pMember, sPlayerBotMgr.BOT_UPDATE_INTERVAL);
					unfrozen = true;
				}

				if (pAI && pAI->m_come_location)
				{
					pAI->m_come_location = false;
					pAI->m_come_location_x = 0.0f;
					pAI->m_come_location_y = 0.0f;
					pAI->m_come_location_z = 0.0f;
				}
			}
		}

		if (unfrozen)
		{
			std::string chatMsg;
			if (arg1)
			{
				std::string choice;
				if (argClass != argClasses.end())
					choice = argClass->first;
				else if (argRole != argRoles.end())
					choice = argRole->first;
				else
					choice = "dps";
				const char* c = choice.c_str();

				if (arg2)
				{
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " {");
					chatMsg += DO_COLOR(COLOR_RED, " except ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } are ");
					chatMsg += DO_COLOR(COLOR_BLUE, "active");
					chatMsg += DO_COLOR(COLOR_WHITE, " again.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
				else if (arg1)
				{
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " are ");
					chatMsg += DO_COLOR(COLOR_BLUE, "active");
					chatMsg += DO_COLOR(COLOR_WHITE, " again.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
			}
			else
			{
				chatMsg += DO_COLOR(COLOR_WHITE, "All ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
				chatMsg += DO_COLOR(COLOR_WHITE, " are ");
				chatMsg += DO_COLOR(COLOR_BLUE, "active");
				chatMsg += DO_COLOR(COLOR_WHITE, " again.");
				PSendSysMessage(chatMsg.c_str());
			}
		}
	}

	// Check if an enemy was selected
	if (!pVictim || pVictim == pPlayer)
	{
		std::string chatMsg;
		chatMsg += DO_COLOR(COLOR_WHITE, "You should select a ");
		chatMsg += DO_COLOR(COLOR_RED, "hostile");
		chatMsg += DO_COLOR(COLOR_WHITE, " target.");
		SendSysMessage(chatMsg.c_str());
		SetSentErrorMessage(true);
		return false;
	}

	// Give attack command
	if (pGroup)
	{
		bool ok = false;
		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				Player* pLeader = nullptr;
				const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
				if (!pAI) continue;
				if (pAI)
				{
					pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (arg1)
				{
					if (argClass != argClasses.end())
					{
						if (arg2)
						{
							if (pMember->getClass() == argClass->second)
								continue;
						}
						else if (pMember->getClass() != argClass->second)
							continue;
					}
					else if (argRole != argRoles.end())
					{
						if (pAI)
						{
							if (arg2)
							{
								if (pAI->m_role == argRole->second)
									continue;
							}
							else if (pAI->m_role != argRole->second)
								continue;
						}
					}
					else
					{
						const std::string option = arg1;
						if (option == "dps")
						{
							if (pAI)
							{
								if (arg2)
								{
									if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
										continue;
								}
								else if (!(pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS))
									continue;
							}
						}
					}
				}

				// Send attack command
				if (pMember->IsValidAttackTarget(pVictim) && pAI && pAI->m_role != ROLE_HEALER)
				{
					//if (pAI->Local_Emote_Timer.Passed() && sWorld.Global_Emote_Timer.Passed()) // TODO FIX
					//{
					//	WorldPacket data(SMSG_TEXT_EMOTE);
					//	if (pMember->getClass() == CLASS_HUNTER)
					//		data << COMPANIONS_EMOTE_OPEN_FIRE;
					//	else
					//		data << COMPANIONS_EMOTE_ATTACK;
					//	data << urand(1, 3);
					//	data << pVictim->GetObjectGuid();
					//	pMember->GetSession()->HandleTextEmoteOpcode(data);
					//	sWorld.Global_Emote_Timer.Reset(100);
					//	pAI->Local_Emote_Timer.Reset(30);
					//}

					pAI->AttackStart(pVictim);
					ok = true;
				}
			}
		}

		if (ok)
		{
			std::string chatMsg;
			if (arg1)
			{
				std::string choice;
				if (argClass != argClasses.end())
					choice = argClass->first;
				else if (argRole != argRoles.end())
					choice = argRole->first;
				else
					choice = "dps";
				const char* c = choice.c_str();

				if (arg2)
				{
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " {");
					chatMsg += DO_COLOR(COLOR_RED, " except ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } will ");
					chatMsg += DO_COLOR(COLOR_BLUE, "attack ");
					chatMsg += "%s";
					PSendSysMessage(chatMsg.c_str(), c, playerLink(pVictim->GetName()).c_str());
				}
				else if (arg1)
				{
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " will ");
					chatMsg += DO_COLOR(COLOR_BLUE, "attack ");
					chatMsg += "%s";
					PSendSysMessage(chatMsg.c_str(), c, playerLink(pVictim->GetName()).c_str());
				}
			}
			else
				PSendSysMessage(DO_COLOR(COLOR_WHITE, "All ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " will ") DO_COLOR(COLOR_BLUE, "attack ") "%s", playerLink(pVictim->GetName()).c_str());
			return true;
		}

		SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
		return ok;
	}

	return false;
}

bool ChatHandler::HandlePartyBotStayCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	if (!pPlayer)
		return false;

	if (pTarget && pTarget != pPlayer)
	{
		Player* pLeader = nullptr;
		const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON());
		if (pAI)
		{
			pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return false;
			}
		}

		if (pLeader && pLeader == pPlayer)
		{
			const bool ok = HandlePartyBotStayHelper(pTarget);
			if (ok)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " will ");
				chatMsg += DO_COLOR(COLOR_BLUE, "stay");
				chatMsg += DO_COLOR(COLOR_WHITE, " at their position.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			else
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is not a valid ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companion");
				chatMsg += DO_COLOR(COLOR_WHITE, " for your command.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			return ok;
		}
	}

	if (Group* pGroup = pPlayer->GetGroup())
	{
		bool ok = false;
		char* arg1 = ExtractArg(&args);
		char* arg2 = ExtractArg(&args);

		auto argClass = argClasses.begin();
		auto argRole = argRoles.begin();

		if (arg1)
		{
			const std::string option = arg1;
			argClass = argClasses.find(option);
			argRole = argRoles.find(option);

			if (argClass == argClasses.end() && argRole == argRoles.end() && option != "dps")
			{
				SendSysMessage("Valid options are: tank, healer, dps, mdps, rdps, warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid.");
				SetSentErrorMessage(true);
				return false;
			}
		}

		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				Player* pLeader = nullptr;
				const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
				if (!pAI) continue;
				if (pAI)
				{
					pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (arg1)
				{
					if (argClass != argClasses.end())
					{
						if (arg2)
						{
							if (pMember->getClass() == argClass->second)
								continue;
						}
						else if (pMember->getClass() != argClass->second)
							continue;
					}
					else if (argRole != argRoles.end())
					{
						if (pAI)
						{
							if (arg2)
							{
								if (pAI->m_role == argRole->second)
									continue;
							}
							else if (pAI->m_role != argRole->second)
								continue;
						}
					}
					else
					{
						const std::string option = arg1;

						if (option == "dps")
						{
							if (pAI)
							{
								if (arg2)
								{
									if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
										continue;
								}
								else if (!(pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS))
									continue;
							}
						}
					}
				}

				if (HandlePartyBotStayHelper(pMember))
					ok = true;
			}
		}

		if (ok)
		{
			if (arg1)
			{
				std::string choice;
				if (argClass != argClasses.end())
					choice = argClass->first;
				else if (argRole != argRoles.end())
					choice = argRole->first;
				else
					choice = "dps";
				const char* c = choice.c_str();

				if (arg2)
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " {");
					chatMsg += DO_COLOR(COLOR_RED, " except ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } will ");
					chatMsg += DO_COLOR(COLOR_BLUE, "stay");
					chatMsg += DO_COLOR(COLOR_WHITE, " at their position.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
				else
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " will ");
					chatMsg += DO_COLOR(COLOR_BLUE, "stay");
					chatMsg += DO_COLOR(COLOR_WHITE, " at their position.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
			}
			else
			{
				std::string chatMsg;
				chatMsg += DO_COLOR(COLOR_WHITE, "All ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
				chatMsg += DO_COLOR(COLOR_WHITE, " will ");
				chatMsg += DO_COLOR(COLOR_BLUE, "stay");
				chatMsg += DO_COLOR(COLOR_WHITE, " at their position.");
				PSendSysMessage(chatMsg.c_str());
			}
		}
		else
			SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
		return ok;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotMoveCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	if (!pPlayer)
		return false;

	if (pTarget && pTarget != pPlayer)
	{
		Player* pLeader = nullptr;
		const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON());
		if (pAI)
		{
			pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return false;
			}
		}

		if (pLeader && pLeader == pPlayer)
		{
			const bool ok = HandlePartyBotMoveHelper(pTarget);
			if (ok)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " will ");
				chatMsg += DO_COLOR(COLOR_BLUE, "move");
				chatMsg += DO_COLOR(COLOR_WHITE, " again.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			else
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " is not a valid ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companion");
				chatMsg += DO_COLOR(COLOR_WHITE, " for your command.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			return ok;
		}
	}

	if (Group* pGroup = pPlayer->GetGroup())
	{
		bool ok = false;
		bool unstaying = false;
		char* arg1 = ExtractArg(&args);
		char* arg2 = ExtractArg(&args);

		auto argClass = argClasses.begin();
		auto argRole = argRoles.begin();

		if (arg1)
		{
			const std::string option = arg1;
			argClass = argClasses.find(option);
			argRole = argRoles.find(option);

			if (argClass == argClasses.end() && argRole == argRoles.end() && option != "dps")
			{
				SendSysMessage("Valid options are: tank, healer, dps, mdps, rdps, warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid.");
				SetSentErrorMessage(true);
				return false;
			}
		}

		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				Player* pLeader = nullptr;
				const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
				if (!pAI) continue;
				if (pAI)
				{
					pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (arg1)
				{
					if (argClass != argClasses.end())
					{
						if (arg2)
						{
							if (pMember->getClass() == argClass->second)
								continue;
						}
						else if (pMember->getClass() != argClass->second)
							continue;
					}
					else if (argRole != argRoles.end())
					{
						if (pAI)
						{
							if (arg2)
							{
								if (pAI->m_role == argRole->second)
									continue;
							}
							else if (pAI->m_role != argRole->second)
								continue;
						}
					}
					else
					{
						const std::string option = arg1;

						if (option == "dps")
						{
							if (pAI)
							{
								if (arg2)
								{
									if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
										continue;
								}
								else if (!(pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS))
									continue;
							}
						}
					}
				}

				if (HandlePartyBotMoveHelper(pMember))
					ok = true;
			}
		}

		if (ok)
		{
			if (arg1)
			{
				std::string choice;
				if (argClass != argClasses.end())
					choice = argClass->first;
				else if (argRole != argRoles.end())
					choice = argRole->first;
				else
					choice = "dps";
				const char* c = choice.c_str();

				if (arg2)
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " {");
					chatMsg += DO_COLOR(COLOR_RED, " except ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } will ");
					chatMsg += DO_COLOR(COLOR_BLUE, "move");
					chatMsg += DO_COLOR(COLOR_WHITE, " again.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
				else
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " will ");
					chatMsg += DO_COLOR(COLOR_BLUE, "move");
					chatMsg += DO_COLOR(COLOR_WHITE, " again.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
			}
			else
			{
				std::string chatMsg;
				chatMsg += DO_COLOR(COLOR_WHITE, "All ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
				chatMsg += DO_COLOR(COLOR_WHITE, " will ");
				chatMsg += DO_COLOR(COLOR_BLUE, "move");
				chatMsg += DO_COLOR(COLOR_WHITE, " again.");
				PSendSysMessage(chatMsg.c_str());
			}
		}
		else
			SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
		return ok;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotAoECommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Unit* pVictim = getSelectedUnit();

	Group* pGroup = pPlayer->GetGroup();
	if (!pGroup)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
		SetSentErrorMessage(true);
		return false;
	}

	// Check if an enemy was selected
	if (!pVictim || pVictim == pPlayer)
	{
		std::string chatMsg;
		chatMsg += DO_COLOR(COLOR_WHITE, "You should select a ");
		chatMsg += DO_COLOR(COLOR_RED, "hostile");
		chatMsg += DO_COLOR(COLOR_WHITE, " target.");
		SendSysMessage(chatMsg.c_str());
		SetSentErrorMessage(true);
		return false;
	}

	// Check arguments
	char* arg1 = ExtractArg(&args);
	char* arg2 = ExtractArg(&args);

	auto argClass = argClasses.begin();
	auto argRole = argRoles.begin();

	if (arg1)
	{
		const std::string option = arg1;
		argClass = argClasses.find(option);
		argRole = argRoles.find(option);

		if (argClass == argClasses.end() && argRole == argRoles.end() && option != "dps")
		{
			SendSysMessage("Valid options are: tank, healer, dps, mdps, rdps, warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid.");
			SetSentErrorMessage(true);
			return false;
		}
	}

	// Give attack command
	if (pGroup)
	{
		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				Player* pLeader = nullptr;
				const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
				if (!pAI) continue;
				if (pAI)
				{
					pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (!pMember->IsValidAttackTarget(pVictim))
					continue;

				if (arg1)
				{
					if (argClass != argClasses.end())
					{
						if (arg2)
						{
							if (pMember->getClass() == argClass->second)
								continue;
						}
						else if (pMember->getClass() != argClass->second)
							continue;
					}
					else if (argRole != argRoles.end())
					{
						if (pAI)
						{
							if (arg2)
							{
								if (pAI->m_role == argRole->second)
									continue;
							}
							else if (pAI->m_role != argRole->second)
								continue;
						}
					}
					else
					{
						const std::string option = arg1;

						if (option == "dps")
						{
							if (pAI)
							{
								if (arg2)
								{
									if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
										continue;
								}
								else if (!(pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS))
									continue;
							}
						}
					}
				}

				if (pMember->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
					continue;

				if (Spell* pSpellGeneric = pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL))
				{
					if (pSpellGeneric->m_spellInfo->SpellName[0] == "Holy Wrath" ||
						pSpellGeneric->m_spellInfo->SpellName[0] == "Multi-Shot" ||
						pSpellGeneric->m_spellInfo->SpellName[0] == "Chain Lightning" ||
						pSpellGeneric->m_spellInfo->SpellName[0] == "Flamestrike")
						continue;
				}

				std::string choice;
				if (argClass != argClasses.end())
					choice = argClass->first;
				else if (argRole != argRoles.end())
					choice = argRole->first;
				else
					choice = "dps";
				const char* c = choice.c_str();
				SpellEntry const* pSpellEntry = nullptr;
				if (pAI)
				{
					if (pAI->AOE_Command_Timer.Passed() && !pMember->IsNonMeleeSpellCasted(true, false, true))
					{
						if (!pSpellEntry && pMember->GetCombatDistance(pVictim) < 10.0f)
						{
							bool ok = false;
							uint32 itemEntry = 0;
							// Goblin Sapper Charge
							if (pAI->IsTier > T0D)
							{
								itemEntry = 10646;
								if (!pMember->HasItemCount(itemEntry, 1))
									pMember->StoreNewItemInBestSlots(itemEntry, 10);
								Item* pItem = pAI->GetItemInInventory(itemEntry);
								if (pItem && !pItem->IsInTrade() && pAI->UseItem(pItem, pVictim) && !ok)
								{
									ok = true;
									ItemPrototype const* pItemProto = sObjectMgr.GetItemPrototype(itemEntry);
									for (const auto Spell : pItemProto->Spells)
										if (!pSpellEntry)
											pSpellEntry = sSpellMgr.GetSpellEntry(Spell.SpellId);
								}
							}

							// Stratholme Holy Water
							if (pAI->IsTier > T0D &&
								pVictim->IsCreature() && pVictim->GetCreatureType() == CREATURE_TYPE_UNDEAD)
							{
								itemEntry = 13180;
								if (!pMember->HasItemCount(itemEntry, 1))
									pMember->StoreNewItemInBestSlots(itemEntry, 10);
								Item* pItem = pAI->GetItemInInventory(itemEntry);
								if (pItem && !pItem->IsInTrade() && pAI->UseItem(pItem, pVictim) && !ok)
								{
									ok = true;
									ItemPrototype const* pItemProto = sObjectMgr.GetItemPrototype(itemEntry);
									for (const auto Spell : pItemProto->Spells)
										if (!pSpellEntry)
											pSpellEntry = sSpellMgr.GetSpellEntry(Spell.SpellId);
								}
							}

							// Dark Iron Bomb
							if (pMember->GetLevel() >= 50)
								itemEntry = 16005;
							// Big Iron Bomb
							else if (pMember->GetLevel() >= 40)
								itemEntry = 4394;
							// Big Bronze Bomb
							else if (pMember->GetLevel() >= 30)
								itemEntry = 4380;
							// Large Copper Bomb
							else if (pMember->GetLevel() >= 20)
								itemEntry = 4370;
							// Rough Copper Bomb
							else if (pMember->GetLevel() >= 10)
								itemEntry = 4360;
							if (!pMember->HasItemCount(itemEntry, 1))
								pMember->StoreNewItemInBestSlots(itemEntry, 10);
							Item* pItem = pAI->GetItemInInventory(itemEntry);
							if (pItem && !pItem->IsInTrade() && pAI->UseItem(pItem, pVictim) && !ok)
							{
								ok = true;
								ItemPrototype const* pItemProto = sObjectMgr.GetItemPrototype(itemEntry);
								for (const auto Spell : pItemProto->Spells)
									if (!pSpellEntry)
										pSpellEntry = sSpellMgr.GetSpellEntry(Spell.SpellId);
							}
							if (ok) pAI->AOE_Command_Timer.Reset(10 * IN_MILLISECONDS);
						}
					}

					if (pSpellEntry)
					{
						std::string chatMsg = "%s";
						if (arg1)
						{
							chatMsg += DO_COLOR(COLOR_WHITE, " { ");
							if (arg2)
								chatMsg += DO_COLOR(COLOR_RED, "except ");
							chatMsg += DO_COLOR(COLOR_GOLD, "%s");
							chatMsg += DO_COLOR(COLOR_WHITE, " }");
						}
						chatMsg += DO_COLOR(COLOR_WHITE, " has ");
						chatMsg += DO_COLOR(COLOR_BLUE, "cast ");
						chatMsg += FormatSpell(pSpellEntry);
						chatMsg += DO_COLOR(COLOR_WHITE, " on ");
						chatMsg += "%s";
						if (arg1)
							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str(), c, playerLink(pVictim->GetName()).c_str());
						else
							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str(), playerLink(pVictim->GetName()).c_str());
						continue;
					}

					if (!pSpellEntry)
					{
						switch (pMember->getClass())
						{
						case CLASS_PALADIN:
						{
							if (pAI->m_spells.paladin.pConsecration)
							{
								if (pMember->GetDistance(pVictim) < 8.0f &&
									pAI->CanTryToCastSpell(pMember, pAI->m_spells.paladin.pConsecration))
								{
									if (pMember->IsNonMeleeSpellCasted(false, false, true))
										pMember->InterruptNonMeleeSpells(false);
									if (pAI->DoCastSpell(pMember, pAI->m_spells.paladin.pConsecration) == SPELL_CAST_OK)
									{
										pSpellEntry = pAI->m_spells.paladin.pConsecration;
										break;
									}
								}
							}
							if (pAI->m_spells.paladin.pHolyWrath)
							{
								if (pMember->GetDistance(pVictim) < 20.0f &&
									pVictim->IsCreature() && (pVictim->GetCreatureType() == CREATURE_TYPE_UNDEAD || pVictim->GetCreatureType() == CREATURE_TYPE_DEMON) &&
									pAI->CanTryToCastSpell(pMember, pAI->m_spells.paladin.pHolyWrath))
								{
									Spell* pSpellGeneric = pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL);
									if (!pSpellGeneric || pSpellGeneric->m_spellInfo->SpellName[0] != "Holy Wrath")
									{
										if (pMember->IsNonMeleeSpellCasted(false, false, true))
											pMember->InterruptNonMeleeSpells(false);
										if (pAI->DoCastSpell(pMember, pAI->m_spells.paladin.pHolyWrath) == SPELL_CAST_OK)
										{
											pSpellEntry = pAI->m_spells.paladin.pHolyWrath;
											break;
										}
									}
								}
							}
							break;
						}
						case CLASS_HUNTER:
						{
							if (pAI->m_spells.hunter.pMultiShot)
							{
								if (pAI->CanTryToCastSpell(pVictim, pAI->m_spells.hunter.pMultiShot))
								{
									Spell* pSpellGeneric = pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL);
									if (!pSpellGeneric || pSpellGeneric->m_spellInfo->SpellName[0] != "Multi-Shot")
									{
										if (pMember->IsNonMeleeSpellCasted(false, false, true))
											pMember->InterruptNonMeleeSpells(false);
										if (pAI->DoCastSpell(pVictim, pAI->m_spells.hunter.pMultiShot) == SPELL_CAST_OK)
										{
											pSpellEntry = pAI->m_spells.hunter.pMultiShot;
											break;
										}
									}
								}
							}
							if (pAI->m_spells.hunter.pVolley)
							{
								if (pAI->CanTryToCastSpell(pVictim, pAI->m_spells.hunter.pVolley))
								{
									Spell* pSpellChanneled = pMember->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
									if (!pSpellChanneled)
									{
										if (pMember->IsNonMeleeSpellCasted(false, false, true))
											pMember->InterruptNonMeleeSpells(false);
										if (pAI->DoCastSpell(pVictim, pAI->m_spells.hunter.pVolley) == SPELL_CAST_OK)
										{
											pSpellEntry = pAI->m_spells.hunter.pVolley;
											pAI->ignore_aoe_checks = true;
											break;
										}
									}
								}
							}
							break;
						}
						case CLASS_ROGUE:
						{
							if (pAI->m_spells.rogue.pBladeFlurry)
							{
								if (pAI->CanTryToCastSpell(pMember, pAI->m_spells.rogue.pBladeFlurry) &&
									pAI->DoCastSpell(pMember, pAI->m_spells.rogue.pBladeFlurry) == SPELL_CAST_OK)
								{
									pSpellEntry = pAI->m_spells.rogue.pBladeFlurry;
									break;
								}
							}
							break;
						}
						case CLASS_SHAMAN:
						{
							if (pAI->m_spells.shaman.pChainLightning)
							{
								if (pAI->CanTryToCastSpell(pVictim, pAI->m_spells.shaman.pChainLightning))
								{
									Spell* pSpellGeneric = pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL);
									if (!pSpellGeneric || pSpellGeneric->m_spellInfo->SpellName[0] != "Chain Lightning")
									{
										if (pMember->IsNonMeleeSpellCasted(false, false, true))
											pMember->InterruptNonMeleeSpells(false);
										if (pAI->DoCastSpell(pVictim, pAI->m_spells.shaman.pChainLightning) == SPELL_CAST_OK)
										{
											pSpellEntry = pAI->m_spells.shaman.pChainLightning;
											break;
										}
									}
								}
							}
							break;
						}
						case CLASS_MAGE:
						{
							if (pAI->IsTier < T3R)
							{
								if (pAI->m_spells.mage.pConeOfCold)
								{
									if (pMember->GetDistance(pVictim) < 10.0f &&
										pAI->CanTryToCastSpell(pVictim, pAI->m_spells.mage.pConeOfCold))
									{
										if (pMember->IsNonMeleeSpellCasted(false, false, true))
											pMember->InterruptNonMeleeSpells(false);
										pMember->SetInFront(pVictim);
										if (pAI->DoCastSpell(pVictim, pAI->m_spells.mage.pConeOfCold) == SPELL_CAST_OK)
										{
											pSpellEntry = pAI->m_spells.mage.pConeOfCold;
											break;
										}
									}
								}
								if (pAI->m_spells.mage.pArcaneExplosion)
								{
									if (pMember->GetDistance(pVictim) < 10.0f &&
										pAI->CanTryToCastSpell(pVictim, pAI->m_spells.mage.pArcaneExplosion))
									{
										if (pMember->IsNonMeleeSpellCasted(false, false, true))
											pMember->InterruptNonMeleeSpells(false);
										if (pAI->DoCastSpell(pVictim, pAI->m_spells.mage.pArcaneExplosion) == SPELL_CAST_OK)
										{
											pSpellEntry = pAI->m_spells.mage.pArcaneExplosion;
											break;
										}
									}
								}
								if (pAI->m_spells.mage.pBlizzard)
								{
									if (pAI->CanTryToCastSpell(pVictim, pAI->m_spells.mage.pBlizzard))
									{
										Spell* pSpellChanneled = pMember->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
										if (!pSpellChanneled)
										{
											if (pMember->IsNonMeleeSpellCasted(false, false, true))
												pMember->InterruptNonMeleeSpells(false);
											if (pAI->DoCastSpell(pVictim, pAI->m_spells.mage.pBlizzard) == SPELL_CAST_OK)
											{
												pSpellEntry = pAI->m_spells.mage.pBlizzard;
												pAI->ignore_aoe_checks = true;
												break;
											}
										}
									}
								}
							}
							else
							{
								if (pAI->m_spells.mage.pBlastWave)
								{
									if (pMember->GetDistance(pVictim) < 10.0f &&
										pAI->CanTryToCastSpell(pVictim, pAI->m_spells.mage.pBlastWave))
									{
										if (pMember->IsNonMeleeSpellCasted(false, false, true))
											pMember->InterruptNonMeleeSpells(false);
										if (pAI->DoCastSpell(pVictim, pAI->m_spells.mage.pBlastWave) == SPELL_CAST_OK)
										{
											pSpellEntry = pAI->m_spells.mage.pBlastWave;
											break;
										}
									}
								}
								if (pAI->m_spells.mage.pArcaneExplosion)
								{
									if (pMember->GetDistance(pVictim) < 10.0f &&
										pAI->CanTryToCastSpell(pVictim, pAI->m_spells.mage.pArcaneExplosion))
									{
										if (pMember->IsNonMeleeSpellCasted(false, false, true))
											pMember->InterruptNonMeleeSpells(false);
										if (pAI->DoCastSpell(pVictim, pAI->m_spells.mage.pArcaneExplosion) == SPELL_CAST_OK)
										{
											pSpellEntry = pAI->m_spells.mage.pArcaneExplosion;
											break;
										}
									}
								}
								if (pAI->m_spells.mage.pFlamestrike)
								{
									if (pAI->CanTryToCastSpell(pVictim, pAI->m_spells.mage.pFlamestrike))
									{
										Spell* pSpellGeneric = pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL);
										if (!pSpellGeneric || pSpellGeneric->m_spellInfo->SpellName[0] != "Flamestrike")
										{
											if (pMember->IsNonMeleeSpellCasted(false, false, true))
												pMember->InterruptNonMeleeSpells(false);
											if (pAI->DoCastSpell(pVictim, pAI->m_spells.mage.pFlamestrike) == SPELL_CAST_OK)
											{
												pSpellEntry = pAI->m_spells.mage.pFlamestrike;
												break;
											}
										}
									}
								}
							}
							break;
						}
						case CLASS_WARLOCK:
						{
							if (pAI->m_spells.warlock.pHellfire)
							{
								if (pMember->GetDistance(pVictim) < 10.0f &&
									pMember->GetHealthPercent() > 50.0f &&
									pAI->CanTryToCastSpell(pVictim, pAI->m_spells.warlock.pHellfire))
								{
									Spell* pSpellChanneled = pMember->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
									if (!pSpellChanneled)
									{
										if (pMember->IsNonMeleeSpellCasted(false, false, true))
											pMember->InterruptNonMeleeSpells(false);
										if (pAI->DoCastSpell(pVictim, pAI->m_spells.warlock.pHellfire) == SPELL_CAST_OK)
										{
											pSpellEntry = pAI->m_spells.warlock.pHellfire;
											pAI->ignore_aoe_checks = true;
											break;
										}
									}
								}
							}
							if (pAI->m_spells.warlock.pRainOfFire)
							{
								if (pAI->CanTryToCastSpell(pVictim, pAI->m_spells.warlock.pRainOfFire))
								{
									Spell* pSpellChanneled = pMember->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
									if (!pSpellChanneled)
									{
										if (pMember->IsNonMeleeSpellCasted(false, false, true))
											pMember->InterruptNonMeleeSpells(false);
										if (pAI->DoCastSpell(pVictim, pAI->m_spells.warlock.pRainOfFire) == SPELL_CAST_OK)
										{
											pSpellEntry = pAI->m_spells.warlock.pRainOfFire;
											pAI->ignore_aoe_checks = true;
											break;
										}
									}
								}
							}
							break;
						}
						case CLASS_DRUID:
						{
							if (pAI->m_spells.druid.pHurricane)
							{
								if (pAI->m_role != ROLE_TANK &&
									pAI->CanTryToCastSpell(pVictim, pAI->m_spells.druid.pHurricane))
								{
									Spell* pSpellChanneled = pMember->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
									if (!pSpellChanneled)
									{
										if (pMember->IsNonMeleeSpellCasted(false, false, true))
											pMember->InterruptNonMeleeSpells(false);
										if (pAI->DoCastSpell(pVictim, pAI->m_spells.druid.pHurricane) == SPELL_CAST_OK)
										{
											pSpellEntry = pAI->m_spells.druid.pHurricane;
											pAI->ignore_aoe_checks = true;
											break;
										}
									}
								}
							}
							break;
						}
						}
					}

					if (pSpellEntry)
					{
						std::string chatMsg = "%s";
						if (arg1)
						{
							chatMsg += DO_COLOR(COLOR_WHITE, " { ");
							if (arg2)
								chatMsg += DO_COLOR(COLOR_RED, "except ");
							chatMsg += DO_COLOR(COLOR_GOLD, "%s");
							chatMsg += DO_COLOR(COLOR_WHITE, " }");
						}
						chatMsg += DO_COLOR(COLOR_WHITE, " has ");
						chatMsg += DO_COLOR(COLOR_BLUE, "cast ");
						chatMsg += FormatSpell(pSpellEntry);
						chatMsg += DO_COLOR(COLOR_WHITE, " on ");
						chatMsg += "%s";
						if (arg1)
							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str(), c, playerLink(pVictim->GetName()).c_str());
						else
							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str(), playerLink(pVictim->GetName()).c_str());
					}
				}
			}
		}
	}
	return true;
}

bool ChatHandler::HandlePartyBotAttackStopCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	if (pTarget && pTarget != pPlayer)
	{
		Player* pLeader = nullptr;
		const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON());
		if (pAI)
		{
			pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return false;
			}
		}

		if (pLeader && pLeader == pPlayer)
		{
			if (!pTarget->HasAuraType(SPELL_AURA_MOD_ROOT) && pAI->IsStaying)
			{
				if (pTarget->hasUnitState(UNIT_STAT_ROOT))
					pTarget->clearUnitState(UNIT_STAT_ROOT);
				pAI->IsStaying = false;
			}

			if (StopPartyBotAttackHelper(pTarget))
			{
				pAI->IsPassive = true;
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " will follow you ");
				chatMsg += DO_COLOR(COLOR_BLUE, "passively.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
				return true;
			}
			PSendSysMessage("%s is not a valid " DO_COLOR(COLOR_ORANGE, "Companion") DO_COLOR(COLOR_WHITE, " for your command."), playerLink(pTarget->GetName()).c_str());
			SetSentErrorMessage(true);
			return false;
		}
	}

	if (Group* pGroup = pPlayer->GetGroup())
	{
		bool ok = false;
		bool unstaying = false;
		char* arg1 = ExtractArg(&args);
		char* arg2 = ExtractArg(&args);

		auto argClass = argClasses.begin();
		auto argRole = argRoles.begin();

		if (arg1)
		{
			const std::string option = arg1;
			argClass = argClasses.find(option);
			argRole = argRoles.find(option);

			if (argClass == argClasses.end() && argRole == argRoles.end() && option != "dps")
			{
				SendSysMessage("Valid options are: tank, healer, dps, mdps, rdps, warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid.");
				SetSentErrorMessage(true);
				return false;
			}
		}

		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				Player* pLeader = nullptr;
				const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
				if (!pAI) continue;
				if (pAI)
				{
					pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (arg1)
				{
					if (argClass != argClasses.end())
					{
						if (arg2)
						{
							if (pMember->getClass() == argClass->second)
								continue;
						}
						else if (pMember->getClass() != argClass->second)
							continue;
					}
					else if (argRole != argRoles.end())
					{
						if (pAI)
						{
							if (arg2)
							{
								if (pAI->m_role == argRole->second)
									continue;
							}
							else if (pAI->m_role != argRole->second)
								continue;
						}
					}
					else
					{
						const std::string option = arg1;

						if (option == "dps")
						{
							if (pAI)
							{
								if (arg2)
								{
									if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
										continue;
								}
								else if (!(pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS))
									continue;
							}
						}
					}
				}

				if (!pMember->HasAuraType(SPELL_AURA_MOD_ROOT) && pAI->IsStaying)
				{
					if (pMember->hasUnitState(UNIT_STAT_ROOT))
						pMember->clearUnitState(UNIT_STAT_ROOT);
					pAI->IsStaying = false;
					unstaying = true;
				}

				if (StopPartyBotAttackHelper(pMember))
				{
					pAI->IsPassive = true;
					ok = true;
				}
			}
		}

		if (ok)
		{
			if (arg1)
			{
				std::string choice;
				if (argClass != argClasses.end())
					choice = argClass->first;
				else if (argRole != argRoles.end())
					choice = argRole->first;
				else
					choice = "dps";
				const char* c = choice.c_str();

				if (arg2)
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " {");
					chatMsg += DO_COLOR(COLOR_RED, " except ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } will follow you ");
					chatMsg += DO_COLOR(COLOR_BLUE, "passively.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
				else
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_WHITE, "{ ");
					chatMsg += DO_COLOR(COLOR_GOLD, "%s");
					chatMsg += DO_COLOR(COLOR_WHITE, " } ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " will follow you ");
					chatMsg += DO_COLOR(COLOR_BLUE, "passively.");
					PSendSysMessage(chatMsg.c_str(), c);
				}
			}
			else
			{
				std::string chatMsg;
				chatMsg += DO_COLOR(COLOR_WHITE, "All ");
				chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
				chatMsg += DO_COLOR(COLOR_WHITE, " will follow you ");
				chatMsg += DO_COLOR(COLOR_BLUE, "passively.");
				PSendSysMessage(chatMsg.c_str());
			}
		}
		else
			SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
		return ok;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotToggleCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	if (!pPlayer)
		return false;

	if (pTarget && pTarget != pPlayer)
	{
		Player* pLeader = nullptr;

		if (const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON()))
		{
			pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return false;
			}
		}

		if (pLeader && pLeader == pPlayer)
		{
			bool ok;
			if (char* arg = ExtractArg(&args))
			{
				const std::string option = arg;

				if (option == "aoe")
				{
					const uint8 OK = HandlePartyBotToggleAOEHelper(pTarget);

					if (OK == 1)
					{
						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " will only use ");
						chatMsg += DO_COLOR(COLOR_CIRCLE, "Single-Target");
						chatMsg += DO_COLOR(COLOR_WHITE, " spells.");
						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
						return true;
					}

					if (OK == 2)
					{
						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " is allowed to use ");
						chatMsg += DO_COLOR(COLOR_SQUARE, "Area-of-Effect");
						chatMsg += DO_COLOR(COLOR_WHITE, " spells.");
						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
						return true;
					}

					std::string chatMsg = "%s";
					chatMsg += DO_COLOR(COLOR_WHITE, " is unaffected by this command. ");
					PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
					return false;
				}

				if (option == "helm")
				{
					ok = HandlePartyBotToggleHelper(pTarget, pPlayer, 1);

					if (ok)
					{
						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " has toggled their Helm.");
						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
					}
					else
					{
						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " has not toggled their Helm, maybe they like it that way.");
						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
					}
					return ok;
				}

				if (option == "cloak")
				{
					ok = HandlePartyBotToggleHelper(pTarget, pPlayer, 2);

					if (ok)
					{
						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " has toggled their Cloak.");
						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
					}
					else
					{
						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " has not toggled their Cloak, maybe they like it that way.");
						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
					}
					return ok;
				}

				if (option == "totems")
				{
					if (pTarget->getClass() != CLASS_SHAMAN)
					{
						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " is not a Shaman.");
						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
						return false;
					}

					const uint8 OK = HandlePartyBotToggleTotemsHelper(pTarget);

					if (OK == 1)
					{
						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " will not use Totems.");
						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
						return true;
					}

					if (OK == 2)
					{
						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " is allowed to use Totems.");
						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
						return true;
					}

					std::string chatMsg = "%s";
					chatMsg += DO_COLOR(COLOR_WHITE, " is unaffected by this command. ");
					PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
					return false;
				}
			}

			ok = HandlePartyBotToggleHelper(pTarget, pPlayer, 3);

			if (ok)
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " has toggled their Helm and Cloak.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			else
			{
				std::string chatMsg = "%s";
				chatMsg += DO_COLOR(COLOR_WHITE, " has not toggled their Helm and Cloak, maybe they like it that way.");
				PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
			}
			return ok;
		}
	}

	if (Group* pGroup = pPlayer->GetGroup())
	{
		bool ok = false;
		char* arg = ExtractArg(&args);

		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				Player* pLeader = nullptr;

				if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
				{
					pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (arg)
				{
					const std::string option = arg;

					if (option == "aoe")
					{
						const uint8 OK = HandlePartyBotToggleAOEHelper(pMember);

						if (OK == 1)
						{
							std::string chatMsg = "%s";
							chatMsg += DO_COLOR(COLOR_WHITE, " will only use ");
							chatMsg += DO_COLOR(COLOR_CIRCLE, "Single-Target");
							chatMsg += DO_COLOR(COLOR_WHITE, " spells.");
							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
							ok = true;
						}
						else if (OK == 2)
						{
							std::string chatMsg = "%s";
							chatMsg += DO_COLOR(COLOR_WHITE, " is allowed to use ");
							chatMsg += DO_COLOR(COLOR_SQUARE, "Area-of-Effect");
							chatMsg += DO_COLOR(COLOR_WHITE, " spells.");
							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
							ok = true;
						}
						else
						{
							std::string chatMsg = "%s";
							chatMsg += DO_COLOR(COLOR_WHITE, " is unaffected by this command. ");
							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
						}
						continue;
					}

					if (option == "helm")
					{
						ok = HandlePartyBotToggleHelper(pMember, pPlayer, 1);
						continue;
					}

					if (option == "cloak")
					{
						ok = HandlePartyBotToggleHelper(pMember, pPlayer, 2);
						continue;
					}

					if (option == "totems")
					{
						if (pMember->getClass() != CLASS_SHAMAN)
						{
							std::string chatMsg = "%s";
							chatMsg += DO_COLOR(COLOR_WHITE, " is not a Shaman.");
							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
							ok = true;
							continue;
						}

						const uint8 OK = HandlePartyBotToggleTotemsHelper(pMember);

						if (OK == 1)
						{
							std::string chatMsg = "%s";
							chatMsg += DO_COLOR(COLOR_WHITE, " will not use Totems.");
							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
							ok = true;
						}
						else if (OK == 2)
						{
							std::string chatMsg = "%s";
							chatMsg += DO_COLOR(COLOR_WHITE, " is allowed to use Totems.");
							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
							ok = true;
						}
						else
						{
							std::string chatMsg = "%s";
							chatMsg += DO_COLOR(COLOR_WHITE, " is unaffected by this command. ");
							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
						}
						continue;
					}
				}

				ok = HandlePartyBotToggleHelper(pMember, pPlayer, 3);
			}
		}

		if (ok)
		{
			if (arg)
			{
				const std::string option = arg;

				if (option == "aoe")
					return ok;
				if (option == "helm")
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " have toggled their Helm.");
					PSendSysMessage(chatMsg.c_str());
					return ok;
				}
				if (option == "cloak")
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "All ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " have toggled their Cloak.");
					PSendSysMessage(chatMsg.c_str());
					return ok;
				}
				if (option == "totems")
					return ok;
			}
			std::string chatMsg;
			chatMsg += DO_COLOR(COLOR_WHITE, "All ");
			chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
			chatMsg += DO_COLOR(COLOR_WHITE, " have toggled their Helm and Cloak.");
			PSendSysMessage(chatMsg.c_str());
		}
		else
		{
			if (arg)
			{
				const std::string option = arg;

				if (option == "helm")
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "No ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " have toggled their Helm, maybe they like it that way.");
					PSendSysMessage(chatMsg.c_str());
					return ok;
				}
				if (option == "cloak")
				{
					std::string chatMsg;
					chatMsg += DO_COLOR(COLOR_WHITE, "No ");
					chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
					chatMsg += DO_COLOR(COLOR_WHITE, " have toggled their Cloak, maybe they like it that way.");
					PSendSysMessage(chatMsg.c_str());
					return ok;
				}
			}

			std::string chatMsg;
			chatMsg += DO_COLOR(COLOR_WHITE, "No ");
			chatMsg += DO_COLOR(COLOR_ORANGE, "Companions");
			chatMsg += DO_COLOR(COLOR_WHITE, " have toggled their Helm and Cloak, maybe they like it that way.");
			PSendSysMessage(chatMsg.c_str());
		}

		return ok;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotFollowCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	if (!pPlayer)
		return false;

	bool ok = false;
	if (pTarget && pTarget != pPlayer)
	{
		if (Group* pGroup = pPlayer->GetGroup())
		{
			if (pGroup == pTarget->GetGroup())
			{
				for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
				{
					if (Player* pMember = itr->getSource())
					{
						if (pMember == pPlayer)
							continue;

						if (pMember == pTarget)
							continue;

						if (!pMember->AI_NYCTERMOON())
							continue;

						const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
						if (!pAI) continue;

						Player* pLeader = pAI->GetPartyLeader();
						if (!pLeader)
						{
							pAI->GetBotEntry()->SetRequestRemoval(true);
							return false;
						}

						if (pLeader && pLeader != pPlayer)
							continue;

						if (!pTarget->IsInCombat() && !pMember->IsInCombat() && pMember->GetCombatDistance(pTarget) < 30.0f)
						{
							pAI->m_follow = pTarget;
							if (pMember->IsMoving())
								pMember->StopMoving();
							if (pMember->GetMotionMaster()->GetCurrentMovementGeneratorType())
							{
								pMember->GetMotionMaster()->Clear(false);
								pMember->GetMotionMaster()->MoveIdle();
							}
							ok = true;
						}
					}
				}
			}
		}
	}

	if (ok)
	{
		PSendSysMessage("All Companions will follow %s.", pTarget->GetName());
		return true;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotUnfollowCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();

	if (!pPlayer)
		return false;

	bool ok = false;
	if (Group* pGroup = pPlayer->GetGroup())
	{
		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == pPlayer)
					continue;

				if (!pMember->AI_NYCTERMOON())
					continue;

				const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
				if (!pAI) continue;

				Player* pLeader = pAI->GetPartyLeader();
				if (!pLeader)
				{
					pAI->GetBotEntry()->SetRequestRemoval(true);
					return false;
				}

				if (pLeader && pLeader != pPlayer)
					continue;

				if (pAI->m_follow)
				{
					pAI->m_follow = nullptr;
					if (pMember->IsMoving())
						pMember->StopMoving();
					if (pMember->GetMotionMaster()->GetCurrentMovementGeneratorType())
					{
						pMember->GetMotionMaster()->Clear(false);
						pMember->GetMotionMaster()->MoveIdle();
					}
					ok = true;
				}
			}
		}
	}

	if (ok)
	{
		SendSysMessage("All Companions are back to following you.");
		return true;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
	SetSentErrorMessage(true);
	return false;
}

bool ChatHandler::HandlePartyBotControlMarkCommand(char* args)
{
	const std::string mark = args;

	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	if (pTarget && pTarget != pPlayer)
	{
		Player* pLeader = nullptr;

		if (const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON()))
		{
			pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return false;
			}
		}

		if (pLeader && pLeader == pPlayer)
		{
			if (pTarget->AI_NYCTERMOON())
			{
				if (auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON()))
				{
					// Show current CC Marks if no argument was passed
					if (mark.empty())
					{
						if (!pAI->m_marksToCC.empty())
						{
							std::string text;
							for (auto& it : pAI->m_marksToCC)
							{
								text += " ";
								text += raidTargetIconsColors[it];
							}

							std::string chatMsg = "%s";
							chatMsg += DO_COLOR(COLOR_WHITE, " CC Marks {");
							chatMsg += text;
							chatMsg += DO_COLOR(COLOR_WHITE, " }");

							PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
							return true;
						}

						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " CC Marks { none }");

						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
						return true;
					}

					// Bad argument - show valid arguments
					const auto itrMark = raidTargetIcons.find(mark);
					if (itrMark == raidTargetIcons.end())
					{
						std::string text;
						for (uint8 i = 0; i <= 7; i++)
						{
							text += " ";
							text += raidTargetIconsColors[static_cast<RaidTargetIcon>(i)];
						}

						std::string chatMsg;
						chatMsg += DO_COLOR(COLOR_WHITE, "Valid Marks are {");
						chatMsg += text;
						chatMsg += DO_COLOR(COLOR_WHITE, " }");

						SendSysMessage(chatMsg.c_str());
						SetSentErrorMessage(true);
						return false;
					}

					// If Mark is already assigned as Focus then clear the Focus Mark
					if (!pAI->m_marksToFocus.empty() && pAI->m_marksToFocus.front() == itrMark->second)
						pAI->m_marksToFocus.clear();

					// Add argument to CC Marks and display all current ones
					pAI->m_marksToCC.insert(itrMark->second);
					if (!pAI->m_marksToCC.empty())
					{
						std::string text;
						for (auto& it : pAI->m_marksToCC)
						{
							text += " ";
							text += raidTargetIconsColors[it];
						}

						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " CC Marks {");
						chatMsg += text;
						chatMsg += DO_COLOR(COLOR_WHITE, " }");

						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
						return true;
					}
				}
			}
			SendSysMessage(DO_COLOR(COLOR_WHITE, "Target is not a ") DO_COLOR(COLOR_ORANGE, "Companion."));
			SetSentErrorMessage(true);
			return false;
		}
	}

	Group* pGroup = pPlayer->GetGroup();
	if (!pGroup)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
		SetSentErrorMessage(true);
		return false;
	}

	bool ok = false;
	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (Player* pMember = itr->getSource())
		{
			if (pMember == pPlayer)
				continue;

			if (pMember->AI_NYCTERMOON())
			{
				if (auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
				{
					Player* pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}

					if (pLeader && pLeader != pPlayer)
						continue;

					// Show current CC Marks if no argument was passed
					if (mark.empty())
					{
						if (!pAI->m_marksToCC.empty())
						{
							std::string text;
							for (auto& it : pAI->m_marksToCC)
							{
								text += " ";
								text += raidTargetIconsColors[it];
							}

							std::string chatMsg = "%s";
							chatMsg += DO_COLOR(COLOR_WHITE, " CC Marks {");
							chatMsg += text;
							chatMsg += DO_COLOR(COLOR_WHITE, " }");

							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str()); ok = true;
							continue;
						}

						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " CC Marks { none }");

						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str()); ok = true;
						continue;
					}

					// Bad argument - show valid arguments
					const auto itrMark = raidTargetIcons.find(mark);
					if (itrMark == raidTargetIcons.end())
					{
						std::string text;
						for (uint8 i = 0; i <= 7; i++)
						{
							text += " ";
							text += raidTargetIconsColors[static_cast<RaidTargetIcon>(i)];
						}

						std::string chatMsg;
						chatMsg += DO_COLOR(COLOR_WHITE, "Valid Marks are {");
						chatMsg += text;
						chatMsg += DO_COLOR(COLOR_WHITE, " }");

						SendSysMessage(chatMsg.c_str());
						SetSentErrorMessage(true);
						return false;
					}

					// If Mark is already assigned as Focus then clear the Focus Mark
					if (!pAI->m_marksToFocus.empty() && pAI->m_marksToFocus.front() == itrMark->second)
						pAI->m_marksToFocus.clear();

					// Add argument to CC Marks and display all current ones
					pAI->m_marksToCC.insert(itrMark->second); ok = true;
					if (!pAI->m_marksToCC.empty())
					{
						std::string text;
						for (auto& it : pAI->m_marksToCC)
						{
							text += " ";
							text += raidTargetIconsColors[it];
						}

						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " CC Marks {");
						chatMsg += text;
						chatMsg += DO_COLOR(COLOR_WHITE, " }");

						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str());
					}
				}
			}
		}
	}

	if (!ok)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
		SetSentErrorMessage(true);
		return false;
	}
	return true;
}

bool ChatHandler::HandlePartyBotFocusMarkCommand(char* args)
{
	const std::string mark = args;

	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	if (pTarget && pTarget != pPlayer)
	{
		Player* pLeader = nullptr;

		if (const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON()))
		{
			pLeader = pAI->GetPartyLeader();
			if (!pLeader)
			{
				pAI->GetBotEntry()->SetRequestRemoval(true);
				return false;
			}
		}

		if (pLeader && pLeader == pPlayer)
		{
			if (pTarget->AI_NYCTERMOON())
			{
				if (auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON()))
				{
					// Show current Focus Marks if no argument was passed
					if (mark.empty())
					{
						if (!pAI->m_marksToFocus.empty())
						{
							std::string text;
							for (auto& it : pAI->m_marksToFocus)
							{
								text += " ";
								text += raidTargetIconsColors[it];
							}

							std::string chatMsg = "%s";
							chatMsg += DO_COLOR(COLOR_WHITE, " Focus Mark {");
							chatMsg += text;
							chatMsg += DO_COLOR(COLOR_WHITE, " }");

							PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
							return true;
						}

						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " Focus Mark { none }");

						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
						return true;
					}

					// Bad argument - show valid arguments
					const auto itrMark = raidTargetIcons.find(mark);
					if (itrMark == raidTargetIcons.end())
					{
						std::string text;
						for (uint8 i = 0; i <= 7; i++)
						{
							text += " ";
							text += raidTargetIconsColors[static_cast<RaidTargetIcon>(i)];
						}

						std::string chatMsg;
						chatMsg += DO_COLOR(COLOR_WHITE, "Valid Marks are {");
						chatMsg += text;
						chatMsg += DO_COLOR(COLOR_WHITE, " }");

						SendSysMessage(chatMsg.c_str());
						SetSentErrorMessage(true);
						return false;
					}

					// If Mark is already assigned as CC then clear the CC Mark
					if (!pAI->m_marksToCC.empty() && pAI->m_marksToCC.find(itrMark->second) != pAI->m_marksToCC.end())
						pAI->m_marksToCC.erase(itrMark->second);

					// Add argument to Focus Marks and display all current ones
					pAI->m_marksToFocus.clear();
					pAI->m_marksToFocus.push_back(itrMark->second);
					if (!pAI->m_marksToFocus.empty())
					{
						std::string text;
						for (auto& it : pAI->m_marksToFocus)
						{
							text += " ";
							text += raidTargetIconsColors[it];
						}

						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " Focus Mark {");
						chatMsg += text;
						chatMsg += DO_COLOR(COLOR_WHITE, " }");

						PSendSysMessage(chatMsg.c_str(), playerLink(pTarget->GetName()).c_str());
						return true;
					}
				}
			}
			SendSysMessage(DO_COLOR(COLOR_WHITE, "Target is not a ") DO_COLOR(COLOR_ORANGE, "Companion."));
			SetSentErrorMessage(true);
			return false;
		}
	}

	Group* pGroup = pPlayer->GetGroup();
	if (!pGroup)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
		SetSentErrorMessage(true);
		return false;
	}

	bool ok = false;
	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (Player* pMember = itr->getSource())
		{
			if (pMember == pPlayer)
				continue;

			if (pMember->AI_NYCTERMOON())
			{
				if (auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
				{
					Player* pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}

					if (pLeader && pLeader != pPlayer)
						continue;

					// Show current Focus Marks if no argument was passed
					if (mark.empty())
					{
						if (!pAI->m_marksToFocus.empty())
						{
							std::string text;
							for (auto& it : pAI->m_marksToFocus)
							{
								text += " ";
								text += raidTargetIconsColors[it];
							}

							std::string chatMsg = "%s";
							chatMsg += DO_COLOR(COLOR_WHITE, " Focus Mark {");
							chatMsg += text;
							chatMsg += DO_COLOR(COLOR_WHITE, " }");

							PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str()); ok = true;
							continue;
						}

						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " Focus Mark { none }");

						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str()); ok = true;
						continue;
					}

					// Bad argument - show valid arguments
					const auto itrMark = raidTargetIcons.find(mark);
					if (itrMark == raidTargetIcons.end())
					{
						std::string text;
						for (uint8 i = 0; i <= 7; i++)
						{
							text += " ";
							text += raidTargetIconsColors[static_cast<RaidTargetIcon>(i)];
						}

						std::string chatMsg;
						chatMsg += DO_COLOR(COLOR_WHITE, "Valid Marks are {");
						chatMsg += text;
						chatMsg += DO_COLOR(COLOR_WHITE, " }");

						SendSysMessage(chatMsg.c_str());
						SetSentErrorMessage(true);
						return false;
					}

					// If Mark is already assigned as CC then clear the CC Mark
					if (!pAI->m_marksToCC.empty() && pAI->m_marksToCC.find(itrMark->second) != pAI->m_marksToCC.end())
						pAI->m_marksToCC.erase(itrMark->second);

					// Add argument to Focus Marks and display all current ones
					pAI->m_marksToFocus.clear();
					pAI->m_marksToFocus.push_back(itrMark->second);
					if (!pAI->m_marksToFocus.empty())
					{
						std::string text;
						for (auto& it : pAI->m_marksToFocus)
						{
							text += " ";
							text += raidTargetIconsColors[it];
						}

						std::string chatMsg = "%s";
						chatMsg += DO_COLOR(COLOR_WHITE, " Focus Mark {");
						chatMsg += text;
						chatMsg += DO_COLOR(COLOR_WHITE, " }");

						PSendSysMessage(chatMsg.c_str(), playerLink(pMember->GetName()).c_str()); ok = true;
					}
				}
			}
		}
	}

	if (!ok)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
		SetSentErrorMessage(true);
		return false;
	}
	return true;
}

bool ChatHandler::HandlePartyBotClearMarksCommand(char* args)
{
	Player* pPlayer = GetSession()->GetPlayer();
	Player* pTarget = getSelectedPlayer();

	if (pTarget && pTarget != pPlayer)
	{
		if (pTarget->AI_NYCTERMOON())
		{
			if (auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI_NYCTERMOON()))
			{
				if (std::strcmp(args, "ccmark") == 0)
				{
					pAI->m_marksToCC.clear();
					PSendSysMessage(DO_COLOR(COLOR_WHITE, "CC Marks cleared for %s"), playerLink(pTarget->GetName()).c_str());
					return true;
				}
				if (std::strcmp(args, "focusmark") == 0)
				{
					pAI->m_marksToFocus.clear();
					PSendSysMessage(DO_COLOR(COLOR_WHITE, "Focus Mark cleared for %s"), playerLink(pTarget->GetName()).c_str());
					return true;
				}

				pAI->m_marksToCC.clear();
				pAI->m_marksToFocus.clear();
				PSendSysMessage(DO_COLOR(COLOR_WHITE, "All Mark assignments cleared for %s"), playerLink(pTarget->GetName()).c_str());
				return true;
			}
		}
		SendSysMessage(DO_COLOR(COLOR_WHITE, "Target is not a ") DO_COLOR(COLOR_ORANGE, "Companion."));
		SetSentErrorMessage(true);
		return false;
	}

	Group* pGroup = pPlayer->GetGroup();
	if (!pGroup)
	{
		SendSysMessage(DO_COLOR(COLOR_WHITE, "You are not in a group."));
		SetSentErrorMessage(true);
		return false;
	}

	bool ok = false;
	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (Player* pMember = itr->getSource())
		{
			if (pMember == pPlayer)
				continue;

			if (pMember->AI_NYCTERMOON())
			{
				if (auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON()))
				{
					Player* pLeader = pAI->GetPartyLeader();
					if (!pLeader)
					{
						pAI->GetBotEntry()->SetRequestRemoval(true);
						return false;
					}

					if (pLeader && pLeader != pPlayer)
						continue;

					if (std::strcmp(args, "ccmark") == 0)
					{
						pAI->m_marksToCC.clear();
					}
					else if (std::strcmp(args, "focusmark") == 0)
					{
						pAI->m_marksToFocus.clear();
					}
					else
					{
						pAI->m_marksToCC.clear();
						pAI->m_marksToFocus.clear();
					}
					ok = true;
				}
			}
		}
	}

	if (ok)
	{
		if (std::strcmp(args, "ccmark") == 0)
		{
			SendSysMessage(DO_COLOR(COLOR_WHITE, "CC Marks cleared for all ") DO_COLOR(COLOR_ORANGE, "Companions."));
			return true;
		}

		if (std::strcmp(args, "focusmark") == 0)
		{
			SendSysMessage(DO_COLOR(COLOR_WHITE, "Focus Mark cleared for all ") DO_COLOR(COLOR_ORANGE, "Companions."));
			return true;
		}

		SendSysMessage(DO_COLOR(COLOR_WHITE, "Mark assignments cleared for all ") DO_COLOR(COLOR_ORANGE, "Companions."));
		return true;
	}

	SendSysMessage(DO_COLOR(COLOR_WHITE, "No ") DO_COLOR(COLOR_ORANGE, "Companions") DO_COLOR(COLOR_WHITE, " are valid for your command."));
	SetSentErrorMessage(true);
	return false;
}