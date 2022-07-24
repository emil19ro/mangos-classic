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
	const QueryResult* result = LoginDatabase.PQuery("SELECT MAX(`id`)" " FROM `account`");
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
	const auto session = new WorldSession(accountID, nullptr, SEC_PLAYER, 0, LOCALE_enUS, e->GetAccountName(), ACCOUNT_FLAG_SHOW_ANTICHEAT);
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
}

/*********************************************************/
/***                    CHAT COMMANDS                  ***/
/*********************************************************/

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
				return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME);

			return PickRandomValue(RACE_ORC, RACE_UNDEAD, RACE_TAUREN, RACE_TROLL);
		}
		case CLASS_PALADIN:
		{
			return urand(0, 1) ? RACE_HUMAN : RACE_DWARF;
		}
		case CLASS_HUNTER:
		{
			if (playerTeam == ALLIANCE)
				return urand(0, 1) ? RACE_DWARF : RACE_NIGHTELF;

			return PickRandomValue(RACE_ORC, RACE_TAUREN, RACE_TROLL);
		}
		case CLASS_ROGUE:
		{
			if (playerTeam == ALLIANCE)
				return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME);

			return PickRandomValue(RACE_ORC, RACE_UNDEAD, RACE_TROLL);
		}
		case CLASS_PRIEST:
		{
			if (playerTeam == ALLIANCE)
				return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF);

			return urand(0, 1) ? RACE_UNDEAD : RACE_TROLL;
		}
		case CLASS_SHAMAN:
		{
			return PickRandomValue(RACE_ORC, RACE_TAUREN, RACE_TROLL);
		}
		case CLASS_MAGE:
		{
			if (playerTeam == ALLIANCE)
				return urand(0, 1) ? RACE_HUMAN : RACE_GNOME;

			return urand(0, 1) ? RACE_UNDEAD : RACE_TROLL;
		}
		case CLASS_WARLOCK:
		{
			if (playerTeam == ALLIANCE)
				return urand(0, 1) ? RACE_HUMAN : RACE_GNOME;

			return urand(0, 1) ? RACE_ORC : RACE_UNDEAD;
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
				if (playerRace == RACE_HUMAN || playerRace == RACE_DWARF || playerRace == RACE_NIGHTELF || playerRace == RACE_GNOME)
					return playerRace;

				return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME);
			}

			if (playerRace == RACE_ORC || playerRace == RACE_UNDEAD || playerRace == RACE_TAUREN || playerRace == RACE_TROLL)
				return playerRace;

			return PickRandomValue(RACE_ORC, RACE_UNDEAD, RACE_TAUREN, RACE_TROLL);
		}
		case CLASS_PALADIN:
		{
			if (playerRace == RACE_HUMAN || playerRace == RACE_DWARF)
				return playerRace;

			return urand(0, 1) ? RACE_HUMAN : RACE_DWARF;
		}
		case CLASS_HUNTER:
		{
			if (playerTeam == ALLIANCE)
			{
				if (playerRace == RACE_DWARF || playerRace == RACE_NIGHTELF)
					return playerRace;

				return urand(0, 1) ? RACE_DWARF : RACE_NIGHTELF;
			}

			if (playerRace == RACE_ORC || playerRace == RACE_TAUREN || playerRace == RACE_TROLL)
				return playerRace;

			return PickRandomValue(RACE_ORC, RACE_TAUREN, RACE_TROLL);
		}
		case CLASS_ROGUE:
		{
			if (playerTeam == ALLIANCE)
			{
				if (playerRace == RACE_HUMAN || playerRace == RACE_DWARF || playerRace == RACE_NIGHTELF || playerRace == RACE_GNOME)
					return playerRace;

				return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME);
			}

			if (playerRace == RACE_ORC || playerRace == RACE_UNDEAD || playerRace == RACE_TROLL)
				return playerRace;

			return PickRandomValue(RACE_ORC, RACE_UNDEAD, RACE_TROLL);
		}
		case CLASS_PRIEST:
		{
			if (playerTeam == ALLIANCE)
			{
				if (playerRace == RACE_HUMAN || playerRace == RACE_DWARF || playerRace == RACE_NIGHTELF)
					return playerRace;

				return PickRandomValue(RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF);
			}

			if (playerRace == RACE_UNDEAD || playerRace == RACE_TROLL)
				return playerRace;

			return urand(0, 1) ? RACE_UNDEAD : RACE_TROLL;
		}
		case CLASS_SHAMAN:
		{
			if (playerRace == RACE_ORC || playerRace == RACE_TAUREN || playerRace == RACE_TROLL)
				return playerRace;

			return PickRandomValue(RACE_ORC, RACE_TAUREN, RACE_TROLL);
		}
		case CLASS_MAGE:
		{
			if (playerTeam == ALLIANCE)
			{
				if (playerRace == RACE_HUMAN || playerRace == RACE_GNOME)
					return playerRace;

				return urand(0, 1) ? RACE_HUMAN : RACE_GNOME;
			}

			if (playerRace == RACE_UNDEAD || playerRace == RACE_TROLL)
			{
				return playerRace;
			}

			return urand(0, 1) ? RACE_UNDEAD : RACE_TROLL;
		}
		case CLASS_WARLOCK:
		{
			if (playerTeam == ALLIANCE)
			{
				if (playerRace == RACE_HUMAN || playerRace == RACE_GNOME)
					return playerRace;

				return urand(0, 1) ? RACE_HUMAN : RACE_GNOME;
			}

			if (playerRace == RACE_ORC || playerRace == RACE_UNDEAD)
				return playerRace;

			return urand(0, 1) ? RACE_ORC : RACE_UNDEAD;
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

	// Handle Class
	if (const char* arg1 = ExtractArg(&args))
	{
		const std::string option = arg1;
		if (option == "warrior")
			botClass = CLASS_WARRIOR;
		else if (option == "paladin" && (pPlayer->GetTeam() == ALLIANCE || pPlayer->GetSession()->GetSecurity() >= SEC_ADMINISTRATOR))
			botClass = CLASS_PALADIN;
		else if (option == "hunter")
			botClass = CLASS_HUNTER;
		else if (option == "rogue")
			botClass = CLASS_ROGUE;
		else if (option == "priest")
			botClass = CLASS_PRIEST;
		else if (option == "shaman" && (pPlayer->GetTeam() == HORDE || pPlayer->GetSession()->GetSecurity() >= SEC_ADMINISTRATOR))
			botClass = CLASS_SHAMAN;
		else if (option == "mage")
			botClass = CLASS_MAGE;
		else if (option == "warlock")
			botClass = CLASS_WARLOCK;
		else if (option == "druid")
			botClass = CLASS_DRUID;
		else if (option == "dps")
		{
			std::vector<uint32> dpsClasses = { CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_MAGE, CLASS_WARLOCK, CLASS_DRUID, CLASS_PRIEST };
			if (pPlayer->GetTeam() == HORDE)
				dpsClasses.push_back(CLASS_SHAMAN);
			else
				dpsClasses.push_back(CLASS_PALADIN);
			botClass = static_cast<uint8>(SelectRandomContainerElement(dpsClasses));
			botRole = CombatBotBaseAI::IsMeleeDamageClass(botClass) ? ROLE_MELEE_DPS : ROLE_RANGE_DPS;
		}
		else if (option == "healer")
		{
			std::vector<uint32> healerClasses = { CLASS_PRIEST, CLASS_DRUID };
			if (pPlayer->GetTeam() == HORDE)
				healerClasses.push_back(CLASS_SHAMAN);
			else
				healerClasses.push_back(CLASS_PALADIN);
			botClass = static_cast<uint8>(SelectRandomContainerElement(healerClasses));
			botRole = ROLE_HEALER;
		}
		else if (option == "tank")
		{
			std::vector<uint32> tankClasses = { CLASS_WARRIOR, CLASS_DRUID };
			if (pPlayer->GetTeam() == HORDE)
				tankClasses.push_back(CLASS_SHAMAN);
			else
				tankClasses.push_back(CLASS_PALADIN);
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
