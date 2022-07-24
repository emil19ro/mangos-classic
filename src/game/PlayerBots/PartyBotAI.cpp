#include "PartyBotAI.h"
#include "PlayerBotMgr.h"
#include "MotionGenerators/PathFinder.h"
#include "World/World.h"

void PartyBotAI::OnPlayerLogin()
{
	if (!m_initialized)
		me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
}

void PartyBotAI::AddToLeaderGroup() const
{
	Player* pLeader = ObjectAccessor::FindPlayer(m_leaderGUID);
	if (!pLeader)
		return;

	Group* pGroup = pLeader->GetGroup();
	if (!pGroup)
	{
		pGroup = new Group;
		if (!pGroup->Create(pLeader->GetObjectGuid(), pLeader->GetName()))
		{
			delete pGroup;
			return;
		}
		sObjectMgr.AddGroup(pGroup);
	}

	pGroup->AddMember(me->GetObjectGuid(), me->GetName());
}

Player* PartyBotAI::GetPartyLeader() const
{
	Group* pGroup = me->GetGroup();
	if (!pGroup)
		return nullptr;

	if (Player* originalLeader = ObjectAccessor::FindPlayerNotInWorld(m_leaderGUID))
	{
		if (me->InBattleGround() == originalLeader->InBattleGround())
		{
			// In case the original spawner is not in the same group as the bots anymore.
			if (pGroup != originalLeader->GetGroup())
				return nullptr;

			// In case the current leader is the bot itself and it's not inside a Battleground.
			const ObjectGuid currentLeaderGuid = pGroup->GetLeaderGuid();
			if (currentLeaderGuid == me->GetObjectGuid() && !me->InBattleGround())
				return nullptr;
		}

		return originalLeader;
	}

	return nullptr;
}

void PartyBotAI::SetFormationPosition(Player* pLeader)
{
	if (!pLeader)
		return;

	uint8 free_position = 1;
	std::set<uint8> held_positions;
	if (Group* pGroup = me->GetGroup())
	{
		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == me)
					continue;

				const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI_NYCTERMOON());
				if (!pAI)
					continue;

				Player* pMemberLeader = pAI->GetPartyLeader();
				if (!pMemberLeader)
					continue;

				if (pMemberLeader != pLeader)
					continue;

				if ((m_role == ROLE_MELEE_DPS || m_role == ROLE_RANGE_DPS) && !(pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS))
					continue;

				if (m_role == ROLE_TANK && pAI->m_role != ROLE_TANK)
					continue;

				if (m_role == ROLE_HEALER && pAI->m_role != ROLE_HEALER)
					continue;

				if (pAI->m_formation_position)
					held_positions.insert(pAI->m_formation_position);
			}
		}
	}
	if (!held_positions.empty())
		for (auto const& itr : held_positions)
			if (itr == free_position)
				free_position = itr + 1;
	m_formation_position = free_position;
}

bool PartyBotAI::OverThreat(Unit* pVictim, const float ThreatPercent)
{
	if (!m_threat_limiter)
		return false;

	if (!pVictim || !pVictim->IsCreature() || pVictim->IsDead() || pVictim->getAttackers().empty())
		return false;

	if (!IsBoss(pVictim))
		return false;

	if (pVictim->IsFlying())
		return false;

	float TankThreatAmount = 1.0f;
	float MyThreatAmount = 1.0f;
	float offset = 0.0f;
	if (!pVictim->CanReachWithMeleeAttack(me))
		offset = 20.0f;

	ThreatList const& tList = pVictim->getThreatManager().getThreatList();
	for (const auto itr : tList)
	{
		if (itr->getUnitGuid().IsPlayer())
		{
			if (Unit* pUnit = pVictim->GetMap()->GetUnit(itr->getUnitGuid()))
			{
				// Get my threat amount
				if (pUnit == me)
					MyThreatAmount = pVictim->getThreatManager().getThreat(pUnit);

				// Get the high amount of tank threat
				if (IsTank(pUnit->ToPlayer()) && pVictim->getThreatManager().getThreat(pUnit) > TankThreatAmount)
					TankThreatAmount = pVictim->getThreatManager().getThreat(pUnit);
			}
		}
	}

	if (TankThreatAmount == 1.0f)
		return false;

	if (MyThreatAmount / TankThreatAmount * 100.0f > ThreatPercent + offset)
	{
		/*std::string chatResponse = " ";
		chatResponse += "OT - " + std::to_string(static_cast<uint32>(MyThreatAmount / TankThreatAmount * 100.0f));
		me->Say(chatResponse, LANG_UNIVERSAL);*/

		return true;
	}

	return false;
}

Unit* PartyBotAI::SelectAttackerToSilence(SpellEntry const* pSpellEntry)
{
	Group* pGroup = me->GetGroup();

	if (!pGroup)
		return nullptr;

	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (Player* pMember = itr->getSource())
		{
			if (pMember->getAttackers().empty())
				continue;

			for (auto const& eachAttacker : pMember->getAttackers())
			{
				if (eachAttacker &&
					eachAttacker->IsAlive() &&
					eachAttacker->IsInterruptable() &&
					IsValidHostileTarget(eachAttacker) &&
					eachAttacker->ToWorldObject() &&
					me->IsWithinDist(eachAttacker->ToWorldObject(), 30.0f) &&
					me->IsWithinLOSInMap(eachAttacker->ToWorldObject()))
				{
					if (pSpellEntry)
					{
						if (!me->HasInArc(eachAttacker, 2 * M_PI_F / 3) && !me->IsMoving())
						{
							me->SetInFront(eachAttacker);
							//me->SendMovementPacket(MSG_MOVE_SET_FACING, false);
						}

						if (!CanTryToCastSpell(eachAttacker, pSpellEntry))
							continue;

						if (AreOthersOnSameTarget(eachAttacker->GetObjectGuid(), false, false, "Interrupt"))
							continue;
					}

					return eachAttacker;
				}
			}
		}
	}

	return nullptr;
}

bool PartyBotAI::ClearSettingsWhenDead()
{
	if (me->IsAlive())
		return false;

	// Clear Paused
	if (m_updateTimer.GetExpiry() > sPlayerBotMgr.BOT_UPDATE_INTERVAL)
		m_updateTimer.Reset(sPlayerBotMgr.BOT_UPDATE_INTERVAL);
	// Clear Passive
	if (IsPassive)
		IsPassive = false;
	// Clear Staying
	if (IsStaying)
	{
		if (!me->HasAuraType(SPELL_AURA_MOD_ROOT) && me->hasUnitState(UNIT_STAT_ROOT))
			me->clearUnitState(UNIT_STAT_ROOT);
		IsStaying = false;
	}
	// Clear Come Toggle
	if (IsComeToggle)
	{
		IsComeToggle = false;
		if (Player* pLeader = GetPartyLeader())
		{
			std::string chatMsg = "%s";
			chatMsg += DO_COLOR(COLOR_WHITE, " has toggled ");
			chatMsg += DO_COLOR(COLOR_BLUE, "come");
			chatMsg += DO_COLOR(COLOR_WHITE, " [");
			chatMsg += DO_COLOR(COLOR_RED, "OFF");
			chatMsg += DO_COLOR(COLOR_WHITE, "]");
			ChatHandler(pLeader).PSendSysMessage(chatMsg.c_str(), ChatHandler(pLeader).playerLink(me->GetName()).c_str());
		}
	}
	// Clear OnWhisper
	if (OnWhisper_CastSpell || OnWhisper_CastSpellTargetGUID || OnWhisper_CastSpellRequester)
	{
		OnWhisper_CastSpell = nullptr;
		OnWhisper_CastSpellTargetGUID = ObjectGuid();
		OnWhisper_CastSpellRequester = nullptr;
	}
	// Don't do anything else while dead
	return true;
}

bool PartyBotAI::InitializeBot()
{
	if (m_initialized)
		return false;

	// Add Bot to Leader's group
	AddToLeaderGroup();

	// If the Bot doesn't have a Leader then despawn it
	Player* pLeader = GetPartyLeader();
	if (!pLeader)
	{
		GetBotEntry()->SetRequestRemoval(true);
		return true;
	}

	// Change the loot method of the group to Free For All when not in a raid group
	if (const Group* pGroup = me->GetGroup())
	{
		if (!pGroup->IsRaidGroup() && pGroup->GetLootMethod() == GROUP_LOOT && pGroup->IsLeader(pLeader->GetObjectGuid()))
		{
			WorldPacket data(CMSG_LOOT_METHOD);
			data << FREE_FOR_ALL;
			data << ObjectGuid();
			data << ITEM_QUALITY_UNCOMMON;
			pLeader->GetSession()->HandleLootMethodOpcode(data);
		}
	}

	// Normally the Bot gets its role during the .z add command, but in case it doesn't then give it a generic role
	if (m_role == ROLE_INVALID)
		AutoAssignRole();

	// Equip Bot and learn spells, talents, skills
	InitializeCharacter(me, pLeader, pLeader->GetLevel(), m_role);
	if (pLeader->GetSession()->GetSecurity() == SEC_ADMINISTRATOR)
		InitializeCharacter(pLeader, pLeader, pLeader->GetLevel(), m_role);

	// Clear all spell containers
	ResetSpellData();

	// Assign Bot's spells to spell containers we can reference
	PopulateSpellData();

	// If a spell has a reagent requirement then add it to the Bot's inventory
	AddAllSpellReagents(me);

	// Determine which mount should the Bot use
	SetMountSelection(); // TODO CHECK IF MOUNTS ARE STILL IN GAME

	// Determine where the Bot should be relative to the Leader
	SetFormationPosition(pLeader);

	// Initialize Focus & CC Marks
	if (m_marksToFocus.empty())
		m_marksToFocus.clear();
	if (m_marksToCC.empty())
		m_marksToCC.clear();
	if (m_role == ROLE_MELEE_DPS || m_role == ROLE_RANGE_DPS)
		m_marksToFocus.push_back(RAID_TARGET_ICON_SKULL);
	m_marksToCC.insert(RAID_TARGET_ICON_MOON);

	// No antisocial Bots
	me->SetSocial(pLeader->GetSocial());

	// Allow the Bot to be attackable again
	if (me->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING))
		me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);

	// Allow the leader to add another Bot
	pLeader->IsHiringCompanion = false;

	// Bot has finished initializing
	m_initialized = true;

	return true;
}

bool PartyBotAI::UseItem(Item* pItem, Unit* pTarget) const
{
	if (!pTarget || !pItem)
		return false;

	for (uint8 i = 0; i <= 4; i++)
	{
		if (const uint32 SpellID = pItem->GetProto()->Spells[i].SpellId)
		{
			if (SpellEntry const* pSpellEntry = sSpellMgr.GetSpellEntry(SpellID))
			{
				if (!me->IsSpellReady(SpellID, pItem->GetProto()))
					return false;

				if (me->HasGCD(pSpellEntry))
					return false;
			}
		}
	}

	if (me->CanUseItem(pItem) != EQUIP_ERR_OK)
		return false;

	if (me->IsNonMeleeSpellCasted(false, false, true))
		return false;

	if (pItem->GetProto())
	{
		SpellCastTargets targets;
		targets.setUnitTarget(pTarget);
		me->CastItemUseSpell(pItem, targets, 1, 0);
		if (m_pDebug && m_pDebug->GetObjectGuid() && ObjectAccessor::FindPlayerNotInWorld(m_pDebug->GetObjectGuid()))
		{
			const std::string chatResponse = ChatHandler(me).GetItemLink(pItem->GetProto());
			me->MonsterWhisper(chatResponse.c_str(), m_pDebug);
		}
		return true;
	}
	return false;
}

Item* PartyBotAI::GetItemInInventory(const uint32 ItemEntry) const
{
	for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; i++)
		if (Item* pItem = me->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
			if (pItem->GetEntry() == ItemEntry)
				return pItem;
	for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
		if (const auto pBag = dynamic_cast<Bag*>(me->GetItemByPos(INVENTORY_SLOT_BAG_0, i)))
			for (uint32 j = 0; j < pBag->GetBagSize(); j++)
				if (Item* pItem = me->GetItemByPos(i, static_cast<uint8>(j)))
					if (pItem->GetEntry() == ItemEntry)
						return pItem;
	return nullptr;
}

bool PartyBotAI::Follow(Player* pTarget) const
{
	if (!pTarget)
		return false;

	if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
		return false;

	if (me->IsWithinDistInMap(pTarget, 100.0f) && !me->IsMoving() && !me->IsDead() && !pTarget->IsDead())
	{
		// If I don't have a victim then follow the leader
		float f_distance = 0.0f;
		float f_angle = 0.0f;

		switch (m_role)
		{
		case ROLE_TANK:
		{
			if (m_formation_position == 1)
			{
				f_distance = 4.0f;
				f_angle = 2 * M_PI_F;
			}
			else if (m_formation_position == 2)
			{
				f_distance = 4.0f;
				f_angle = M_PI_F / 4;
			}
			else if (m_formation_position == 3)
			{
				f_distance = 4.0f;
				f_angle = 7.0f * M_PI_F / 4.0f;
			}
			else if (m_formation_position == 4)
			{
				f_distance = 2.0f;
				f_angle = 2 * M_PI_F;
			}
			else
			{
				f_distance = 0.0f;
				f_angle = 0.0f;
			}
			break;
		}
		case ROLE_HEALER:
		{
			if (m_formation_position == 1)
			{
				f_distance = 4.0f;
				f_angle = M_PI_F;
			}
			else if (m_formation_position == 2)
			{
				f_distance = 4.0f;
				f_angle = 3.0f / 4.0f * M_PI_F;
			}
			else if (m_formation_position == 3)
			{
				f_distance = 4.0f;
				f_angle = 5.0f / 4.0f * M_PI_F;
			}
			else if (m_formation_position == 4)
			{
				f_distance = 2.0f;
				f_angle = M_PI_F;
			}
			else
			{
				f_distance = 0.0f;
				f_angle = 0.0f;
			}
			break;
		}
		case ROLE_MELEE_DPS:
		case ROLE_RANGE_DPS:
		{
			if (m_formation_position == 1)
			{
				f_distance = 2.0f;
				f_angle = M_PI_F / 2.0f;
			}
			else if (m_formation_position == 2)
			{
				f_distance = 2.0f;
				f_angle = M_PI_F * 3.0f / 2.0f;
			}
			else if (m_formation_position == 3)
			{
				f_distance = 4.0f;
				f_angle = M_PI_F / 2.0f;
			}
			else if (m_formation_position == 4)
			{
				f_distance = 4.0f;
				f_angle = M_PI_F * 3.0f / 2.0f;
			}
			else
			{
				f_distance = 0.0f;
				f_angle = 0.0f;
			}
			break;
		}
		}

		if (m_follow_distance >= 0.0f)
			f_distance = m_follow_distance;

		if (m_follow_angle >= 0.0f)
			f_angle = m_follow_angle;

		me->GetMotionMaster()->Clear(false);
		me->GetMotionMaster()->MoveFollow(pTarget, f_distance, f_angle);
		return true;
	}
	return false;
}

bool PartyBotAI::Teleport(const Player* pTarget) const
{
	if (!pTarget)
		return false;

	if (!IsStaying && !me->IsInCombat() && !me->IsActualFlying() && (!me->IsWithinDistInMap(pTarget, 100.0f) || !FindPathToTarget(me, pTarget)) &&
		pTarget->IsAlive() && pTarget->IsInWorld() && !pTarget->IsInCombat() && !pTarget->IsTaxiFlying() && !pTarget->IsActualFlying())
	{
		if (!me->IsStopped())
			me->StopMoving();
		if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
		{
			me->GetMotionMaster()->Clear(false);
			me->GetMotionMaster()->MoveIdle();
		}
		char name[128] = {};
		strcpy_s(name, pTarget->GetName());
		ChatHandler(me).HandleGonameCommand(name);
		return true;
	}
	return false;
}

InstanceType PartyBotAI::IsInInstanceType(const uint32 ZoneID, const uint32 AreaID)
{
	//End-Game Dungeons
	//The Temple of Atal'Hakkar - 1477
	//Blackrock Depths - 1584
	//Blackrock Spire - 1583
	//Scholomance - 2057
	//Stratholme - 2017
	//Dire Maul - 2557

	if (
		ZoneID == 1477 ||
		ZoneID == 1584 ||
		ZoneID == 1583 ||
		ZoneID == 2057 ||
		ZoneID == 2017 ||
		ZoneID == 2557)
		return INSTANCE_TYPE_END_DUNGEON;

	//End-Game Raids
	//GM Island - 876
	//Molten Core - 2717
	//Onyxia's Lair - 2159
	//Blackwing Lair - 2677
	//Zul'Gurub - 1977
	//Temple of Ahn'Qiraj - 3428
	//Ruins of Ahn'Qiraj - 3429
	//Naxxramas - 3456
	//Emeriss - 10/856
	//Ysondre - 47/356
	//Lethon - 357/1111
	//Taerar - 331/438
	//Kazzak - 4/73
	//Azuregos - 16/16

	if (
		ZoneID == 876 ||
		ZoneID == 2717 ||
		ZoneID == 2159 ||
		ZoneID == 2677 ||
		ZoneID == 1977 ||
		ZoneID == 3428 ||
		ZoneID == 3429 ||
		ZoneID == 3456 ||
		ZoneID == 10 && AreaID == 856 ||
		ZoneID == 47 && AreaID == 356 ||
		ZoneID == 357 && AreaID == 1111 ||
		ZoneID == 331 && AreaID == 438 ||
		ZoneID == 4 && AreaID == 73 ||
		ZoneID == 16 && AreaID == 16)
		return INSTANCE_TYPE_RAID;

	return INSTANCE_TYPE_NONE;
}

uint8 PartyBotAI::GetCountVoiceTypes(const std::string& VoiceType, const uint8 Race, const uint8 Gender)
{
	if (VoiceType == "Incoming")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_DWARF:
		case RACE_NIGHTELF:
		case RACE_UNDEAD:
		case RACE_TROLL:
			return 2;
		case RACE_GNOME:
			return 1;
		case RACE_ORC:
		{
			if (Gender == GENDER_MALE)
				return 3;
			return 2;
		}
		case RACE_TAUREN:
		{
			if (Gender == GENDER_MALE)
				return 2;
			return 3;
		}
		}
	}
	else if (VoiceType == "Charge")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_DWARF:
		case RACE_UNDEAD:
		case RACE_TROLL:
			return 2;
		case RACE_NIGHTELF:
		{
			if (Gender == GENDER_MALE)
				return 2;
			return 3;
		}
		case RACE_GNOME:
		case RACE_TAUREN:
		{
			if (Gender == GENDER_MALE)
				return 3;
			return 2;
		}
		case RACE_ORC:
			return 3;
		}
	}
	else if (VoiceType == "Flee")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_NIGHTELF:
		case RACE_ORC:
		case RACE_UNDEAD:
		case RACE_TAUREN:
		case RACE_TROLL:
			return 2;
		case RACE_DWARF:
		{
			if (Gender == GENDER_MALE)
				return 3;
			return 2;
		}
		case RACE_GNOME:
			return 3;
		}
	}
	else if (VoiceType == "Hello")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_DWARF:
		case RACE_GNOME:
		{
			if (Gender == GENDER_MALE)
				return 4;
			return 3;
		}
		case RACE_NIGHTELF:
		{
			if (Gender == GENDER_MALE)
				return 3;
			return 4;
		}
		case RACE_ORC:
		case RACE_UNDEAD:
		case RACE_TAUREN:
		case RACE_TROLL:
			return 3;
		}
	}
	else if (VoiceType == "Attack")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_GNOME:
		case RACE_UNDEAD:
		case RACE_TAUREN:
		case RACE_TROLL:
			return 2;
		case RACE_DWARF:
		{
			if (Gender == GENDER_MALE)
				return 3;
			return 2;
		}
		case RACE_NIGHTELF:
		{
			if (Gender == GENDER_MALE)
				return 2;
			return 3;
		}
		case RACE_ORC:
			return 3;
		}
	}
	else if (VoiceType == "Out of Mana")
	{
		return 2;
	}
	else if (VoiceType == "Follow me")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_NIGHTELF:
		case RACE_ORC:
		case RACE_UNDEAD:
		case RACE_TAUREN:
		case RACE_TROLL:
			return 2;
		case RACE_DWARF:
		{
			if (Gender == GENDER_MALE)
				return 3;
			return 2;
		}
		case RACE_GNOME:
		{
			if (Gender == GENDER_MALE)
				return 2;
			return 1;
		}
		}
	}
	else if (VoiceType == "Wait here")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_TROLL:
			return 3;
		case RACE_DWARF:
		case RACE_TAUREN:
		{
			if (Gender == GENDER_MALE)
				return 3;
			return 2;
		}
		case RACE_NIGHTELF:
		case RACE_GNOME:
		case RACE_ORC:
		case RACE_UNDEAD:
			return 2;
		}
	}
	else if (VoiceType == "Heal me")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_NIGHTELF:
		case RACE_GNOME:
		case RACE_ORC:
		case RACE_UNDEAD:
		case RACE_TAUREN:
			return 2;
		case RACE_DWARF:
		case RACE_TROLL:
			return 3;
		}
	}
	else if (VoiceType == "Cheer")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_NIGHTELF:
		case RACE_GNOME:
		case RACE_ORC:
		case RACE_UNDEAD:
		case RACE_TAUREN:
			return 2;
		case RACE_DWARF:
		case RACE_TROLL:
		{
			if (Gender == GENDER_MALE)
				return 3;
			return 2;
		}
		}
	}
	else if (VoiceType == "Open fire")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_DWARF:
		case RACE_NIGHTELF:
		case RACE_UNDEAD:
		case RACE_TAUREN:
		case RACE_TROLL:
		case RACE_GNOME:
			return 2;
		case RACE_ORC:
		{
			if (Gender == GENDER_MALE)
				return 2;
			return 3;
		}
		}
	}
	else if (VoiceType == "Goodbye")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_DWARF:
		case RACE_NIGHTELF:
		case RACE_ORC:
		case RACE_UNDEAD:
		case RACE_TAUREN:
		case RACE_TROLL:
			return 3;
		case RACE_GNOME:
			return 4;
		}
	}
	else if (VoiceType == "Yes")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_NIGHTELF:
		case RACE_GNOME:
		case RACE_UNDEAD:
		case RACE_TAUREN:
			return 3;
		case RACE_DWARF:
		case RACE_TROLL:
		{
			if (Gender == GENDER_MALE)
				return 4;
			return 3;
		}
		case RACE_ORC:
			return 4;
		}
	}
	else if (VoiceType == "No")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_DWARF:
		case RACE_TROLL:
		{
			if (Gender == GENDER_MALE)
				return 4;
			return 3;
		}
		case RACE_NIGHTELF:
		case RACE_GNOME:
		case RACE_ORC:
		case RACE_UNDEAD:
		case RACE_TAUREN:
			return 3;
		}
	}
	else if (VoiceType == "Thank You")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_NIGHTELF:
		case RACE_GNOME:
		case RACE_ORC:
		case RACE_UNDEAD:
		case RACE_TROLL:
			return 3;
		case RACE_DWARF:
			return 4;
		case RACE_TAUREN:
		{
			if (Gender == GENDER_MALE)
				return 4;
			return 3;
		}
		}
	}
	else if (VoiceType == "Youre Welcome")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_DWARF:
		case RACE_NIGHTELF:
		case RACE_GNOME:
		case RACE_ORC:
		case RACE_TAUREN:
		case RACE_TROLL:
			return 3;
		case RACE_UNDEAD:
		{
			if (Gender == GENDER_MALE)
				return 2;
			return 3;
		}
		}
	}
	else if (VoiceType == "Congratulations")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		case RACE_NIGHTELF:
		case RACE_ORC:
		case RACE_TAUREN:
		case RACE_TROLL:
			return 3;
		case RACE_DWARF:
		{
			if (Gender == GENDER_MALE)
				return 5;
			return 4;
		}
		case RACE_GNOME:
		case RACE_UNDEAD:
		{
			if (Gender == GENDER_MALE)
				return 3;
			return 4;
		}
		}
	}
	else if (VoiceType == "Flirt")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		{
			if (Gender == GENDER_MALE)
				return 6;
			return 3;
		}
		case RACE_DWARF:
		case RACE_TAUREN:
		{
			if (Gender == GENDER_MALE)
				return 6;
			return 5;
		}
		case RACE_NIGHTELF:
		{
			if (Gender == GENDER_MALE)
				return 5;
			return 4;
		}
		case RACE_GNOME:
		case RACE_TROLL:
		{
			if (Gender == GENDER_MALE)
				return 4;
			return 5;
		}
		case RACE_ORC:
		case RACE_UNDEAD:
			return 6;
		}
	}
	else if (VoiceType == "Silly")
	{
		switch (Race)
		{
		case RACE_HUMAN:
		{
			if (Gender == GENDER_MALE)
				return 5;
			return 6;
		}
		case RACE_DWARF:
		{
			if (Gender == GENDER_MALE)
				return 6;
			return 5;
		}
		case RACE_NIGHTELF:
		{
			if (Gender == GENDER_MALE)
				return 7;
			return 4;
		}
		case RACE_GNOME:
		{
			if (Gender == GENDER_MALE)
				return 5;
			return 3;
		}
		case RACE_ORC:
			return 5;
		case RACE_TAUREN:
		{
			if (Gender == GENDER_MALE)
				return 4;
			return 3;
		}
		case RACE_TROLL:
		{
			if (Gender == GENDER_MALE)
				return 5;
			return 4;
		}
		case RACE_UNDEAD:
		{
			if (Gender == GENDER_MALE)
				return 4;
			return 7;
		}
		}
	}
	return 1;
}

bool PartyBotAI::IsFocusMarked(Unit* pVictim, Player* pPlayer) const
{
	if (!pVictim || !pPlayer)
		return false;

	Group* pGroup = pPlayer->GetGroup();
	if (!pGroup)
		return false;

	if (const auto pAI = dynamic_cast<PartyBotAI*>(pPlayer->AI()))
	{
		if (!pAI->m_marksToFocus.empty())
		{
			for (auto const& markId : pAI->m_marksToFocus)
			{
				ObjectGuid targetGuid = pPlayer->GetGroup()->GetTargetWithIcon(markId);
				if (targetGuid && targetGuid.IsUnit())
					if (Unit* pUnit = pPlayer->GetMap()->GetUnit(targetGuid))
						if (pVictim == pUnit)
							if (pUnit->IsInCombat() &&
								IsValidHostileTarget(pUnit) &&
								pPlayer->IsWithinDist(pUnit, 50.0f) &&
								pPlayer->IsWithinLOSInMap(pUnit))
								return true;
			}
		}
	}

	if (!pPlayer->AI())
		return true;

	return false;
}

void PartyBotAI::Emote(const CompanionEmotes Emote, Player* pTarget)
{
	Player* pLeader = GetPartyLeader();
	if (!pLeader)
		return;

	// Emote cooldown
	if (const auto pAI = dynamic_cast<PartyBotAI*>(me->AI_NYCTERMOON()))
	{
		pAI->Local_Emote_Timer.Update(1);
		pAI->Threat_Emote_Timer.Update(1);

		if (m_threat_limiter && pAI->Threat_Emote_Timer.Passed() && (m_role == ROLE_MELEE_DPS || m_role == ROLE_RANGE_DPS) && IsInInstanceType(me->GetZoneId(), me->GetAreaId()) == INSTANCE_TYPE_RAID)
		{
			if (Unit* pBoss = me->GetVictim())
			{
				if (IsBoss(pBoss) && pBoss->IsAlive() && !pBoss->getAttackers().empty() && !pBoss->IsFlying())
				{
					float TankThreatAmount = 1.0f;
					float MyThreatAmount = 1.0f;
					Player* pTank = nullptr;

					ThreatList const& tList = pBoss->getThreatManager().getThreatList();
					for (const auto itr : tList)
					{
						if (itr->getUnitGuid().IsPlayer())
						{
							if (Player* pBossAttacker = pBoss->GetMap()->GetUnit(itr->getUnitGuid())->ToPlayer())
							{
								// Get my threat amount
								if (pBossAttacker == me)
									MyThreatAmount = pBoss->getThreatManager().getThreat(pBossAttacker);

								// Get the highest amount of tank threat
								if (IsTank(pBossAttacker) && pBoss->getThreatManager().getThreat(pBossAttacker) > TankThreatAmount)
								{
									TankThreatAmount = pBoss->getThreatManager().getThreat(pBossAttacker);
									pTank = pBossAttacker;
								}
							}
						}
					}

					if (pTank)
					{
						std::string range;
						float current = MyThreatAmount / TankThreatAmount * 100.0f;
						float max = m_threat_threshold + 20.0f;
						if (!pBoss->CanReachWithMeleeAttack(me))
							max = m_threat_threshold + 40.0f;
						range = std::to_string(static_cast<uint32>(max));

						if (current > max)
						{
							std::string ChatResponse = "OT: " + std::to_string(static_cast<uint8>(current)) + "/" + range + "% Tank: " + pTank->GetName() + " Leader: " + pLeader->GetName();
							me->Say(ChatResponse, LANG_UNIVERSAL);
							pAI->Threat_Emote_Timer.Reset(10);
						}
					}
				}
			}
		}

		if (pAI->Local_Emote_Timer.Passed())
		{
			constexpr uint32 Local_Delay = 30;

			if (Emote == COMPANIONS_EMOTE_CHARGE)
			{
				WorldPacket data(SMSG_TEXT_EMOTE);
				data << COMPANIONS_EMOTE_CHARGE;
				data << urand(1, GetCountVoiceTypes("Charge", me->getRace(), me->getGender()));
				data << pLeader->GetObjectGuid();
				me->GetSession()->HandleTextEmoteOpcode(data);

				pAI->Local_Emote_Timer.Reset(Local_Delay);
				return;
			}

			if (Emote == COMPANIONS_EMOTE_CHEER)
			{
				WorldPacket data(SMSG_TEXT_EMOTE);
				data << COMPANIONS_EMOTE_CHEER;
				data << urand(1, GetCountVoiceTypes("Cheer", me->getRace(), me->getGender()));
				data << pLeader->GetObjectGuid();
				me->GetSession()->HandleTextEmoteOpcode(data);

				pAI->Local_Emote_Timer.Reset(Local_Delay);
				return;
			}

			if (Emote == COMPANIONS_EMOTE_CONGRATULATIONS)
			{
				WorldPacket data(SMSG_TEXT_EMOTE);
				data << COMPANIONS_EMOTE_CONGRATULATIONS;
				data << urand(1, GetCountVoiceTypes("Congratulations", me->getRace(), me->getGender()));
				data << pLeader->GetObjectGuid();
				me->GetSession()->HandleTextEmoteOpcode(data);

				pAI->Local_Emote_Timer.Reset(Local_Delay);
				return;
			}

			if (Emote == COMPANIONS_EMOTE_YOURE_WELCOME)
			{
				Player* pWelcome = pLeader;
				if (pTarget) pWelcome = pTarget;

				WorldPacket data(SMSG_TEXT_EMOTE);
				data << COMPANIONS_EMOTE_YOURE_WELCOME;
				data << urand(1, GetCountVoiceTypes("Youre Welcome", me->getRace(), me->getGender()));
				data << pWelcome->GetObjectGuid();
				me->GetSession()->HandleTextEmoteOpcode(data);
				return;
			}

			if (me->IsInCombat())
			{
				if (m_role != ROLE_TANK && GetAttackersInRangeCount(8.0f))
				{
					WorldPacket data(SMSG_TEXT_EMOTE);
					data << COMPANIONS_EMOTE_INCOMING;
					data << urand(1, GetCountVoiceTypes("Incoming", me->getRace(), me->getGender()));
					data << pLeader->GetObjectGuid();
					me->GetSession()->HandleTextEmoteOpcode(data);

					pAI->Local_Emote_Timer.Reset(Local_Delay);
					return;
				}

				if (IsPassive && !me->getAttackers().empty() && !me->IsMounted())
				{
					WorldPacket data(SMSG_TEXT_EMOTE);
					data << COMPANIONS_EMOTE_FLEE;
					data << urand(1, GetCountVoiceTypes("Flee", me->getRace(), me->getGender()));
					data << pLeader->GetObjectGuid();
					me->GetSession()->HandleTextEmoteOpcode(data);

					pAI->Local_Emote_Timer.Reset(Local_Delay);
					return;
				}

				if (me->GetHealthPercent() < 20.0f)
				{
					WorldPacket data(SMSG_TEXT_EMOTE);
					data << COMPANIONS_EMOTE_HEAL_ME;
					data << urand(1, GetCountVoiceTypes("Heal me", me->getRace(), me->getGender()));
					data << pLeader->GetObjectGuid();
					me->GetSession()->HandleTextEmoteOpcode(data);

					pAI->Local_Emote_Timer.Reset(Local_Delay);
					return;
				}

				if (me->GetPowerPercent(POWER_MANA) < 20.0f)
				{
					WorldPacket data(SMSG_TEXT_EMOTE);
					data << COMPANIONS_EMOTE_OUT_OF_MANA;
					data << urand(1, GetCountVoiceTypes("Out of Mana", me->getRace(), me->getGender()));
					data << pLeader->GetObjectGuid();
					me->GetSession()->HandleTextEmoteOpcode(data);

					pAI->Local_Emote_Timer.Reset(Local_Delay);
					return;
				}
			}
		}
	}
}

uint32 PartyBotAI::TradeHealthstone() const
{
	const uint32 MyLevel = me->GetLevel();

	if (MyLevel >= 58)
	{
		if (me->HasSpell(18693)) // Improved Healthstone Rank 2
			return 19013;

		if (me->HasSpell(18692)) // Improved Healthstone Rank 1
			return 19012;

		return 9421; // NO Improved Healthstone
	}
	if (MyLevel >= 46)
	{
		if (me->HasSpell(18693)) // Improved Healthstone Rank 2
			return 19011;

		if (me->HasSpell(18692)) // Improved Healthstone Rank 1
			return 19010;

		return 5510; // NO Improved Healthstone
	}
	if (MyLevel >= 34)
	{
		if (me->HasSpell(18693)) // Improved Healthstone Rank 2
			return 19009;

		if (me->HasSpell(18692)) // Improved Healthstone Rank 1
			return 19008;

		return 5509; // NO Improved Healthstone
	}
	if (MyLevel >= 22)
	{
		if (me->HasSpell(18693)) // Improved Healthstone Rank 2
			return 19007;

		if (me->HasSpell(18692)) // Improved Healthstone Rank 1
			return 19006;

		return 5511; // NO Improved Healthstone
	}
	if (MyLevel >= 10)
	{
		if (me->HasSpell(18693)) // Improved Healthstone Rank 2
			return 19005;

		if (me->HasSpell(18692)) // Improved Healthstone Rank 1
			return 19004;

		return 5512; // NO Improved Healthstone
	}

	return 0;
}

uint32 PartyBotAI::TradeConjuredFood() const
{
	const uint32 MyLevel = me->GetLevel();

	if (MyLevel >= 60)
		return 22895;

	if (MyLevel >= 50)
		return 8076;

	if (MyLevel >= 40)
		return 8075;

	if (MyLevel >= 30)
		return 1487;

	if (MyLevel >= 20)
		return 1114;

	if (MyLevel >= 10)
		return 1113;

	if (MyLevel >= 1)
		return 5349;

	return 0;
}

uint32 PartyBotAI::TradeConjuredWater() const
{
	const uint32 MyLevel = me->GetLevel();

	if (MyLevel >= 60)
		return 8079;

	if (MyLevel >= 50)
		return 8078;

	if (MyLevel >= 40)
		return 8077;

	if (MyLevel >= 30)
		return 3772;

	if (MyLevel >= 20)
		return 2136;

	if (MyLevel >= 10)
		return 2288;

	if (MyLevel >= 1)
		return 5350;

	return 0;
}

uint32 PartyBotAI::LevelAppropriateFood() const
{
	const uint32 MyLevel = me->GetLevel();

	//https://classic.wowhead.com/item=21215/graccus-mince-meat-fruitcake
	if (MyLevel >= 60 && IsTier > T0D)
		return 25990;

	//https://classic.wowhead.com/item=19301/alterac-manna-biscuit
	if (MyLevel >= 50)
		return 23692;

	//https://classic.wowhead.com/item=8076/conjured-sweet-roll
	if (MyLevel >= 45)
		return 1131;

	//https://classic.wowhead.com/item=8075/conjured-sourdough
	if (MyLevel >= 35)
		return 1129;

	//https://classic.wowhead.com/item=1487/conjured-pumpernickel
	if (MyLevel >= 25)
		return 1127;

	//https://classic.wowhead.com/item=1114/conjured-rye
	if (MyLevel >= 15)
		return 435;

	//https://classic.wowhead.com/item=1113/conjured-bread
	if (MyLevel >= 5)
		return 434;

	//https://classic.wowhead.com/item=5349/conjured-muffin
	if (MyLevel >= 1)
		return 433;

	return 23692;
}

uint32 PartyBotAI::LevelAppropriateDrink() const
{
	const uint32 MyLevel = me->GetLevel();

	//https://classic.wowhead.com/item=21215/graccus-mince-meat-fruitcake
	if (MyLevel >= 60 && IsTier > T0D)
		return 25990;

	//https://classic.wowhead.com/item=19301/alterac-manna-biscuit
	if (MyLevel >= 50)
		return 23692;

	//https://classic.wowhead.com/item=8078/conjured-sparkling-water
	if (MyLevel >= 45)
		return 1137;

	//https://classic.wowhead.com/item=8077/conjured-mineral-water
	if (MyLevel >= 35)
		return 1135;

	//https://classic.wowhead.com/item=3772/conjured-spring-water
	if (MyLevel >= 25)
		return 1133;

	//https://classic.wowhead.com/item=2136/conjured-purified-water
	if (MyLevel >= 15)
		return 432;

	//https://classic.wowhead.com/item=2288/conjured-fresh-water
	if (MyLevel >= 5)
		return 431;

	//https://classic.wowhead.com/item=5350/conjured-water
	if (MyLevel >= 1)
		return 430;

	return 23692;
}

LeaderMountType PartyBotAI::GetLeaderMountType(const Player* pLeader) const
{
	if (!pLeader || !pLeader->IsMounted())
		return NONE;

	const auto& auraList = pLeader->GetAurasByType(SPELL_AURA_MOUNTED);
	if (!auraList.empty())
	{
		if (me->GetMapId() == 530) // Outland
		{
			if ((*auraList.begin())->GetSpellProto()->EffectBasePoints[1] == 279)
				return FLYING_FAST;
			return FLYING_SLOW;
		}
		if ((*auraList.begin())->GetSpellProto()->EffectBasePoints[1] == 99)
			return GROUND_FAST;
		return GROUND_SLOW;
	}

	return NONE;
}

void PartyBotAI::MountUp(const Player* pLeader) const
{
	if (me->IsInCombat()) 
		return;

	if (!me->IsMounted())
	{
		LeaveCombatDruidForm();
		if (me->HasStealthAura())
			me->RemoveAurasDueToSpellByCancel(me->GetAurasByType(SPELL_AURA_MOD_STEALTH).front()->GetId());

		uint32 Mount = 0;
		const auto MountType = GetLeaderMountType(pLeader);
		if (me->GetZoneId() == 3429 || me->GetZoneId() == 3428) // Ahn'Qiraj
			Mount = PickRandomValue(26656, 26054, 26055, 25953, 26056);
		else if (MountType == FLYING_FAST)
			Mount = mount_flying_spell_id_280;
		else if (MountType == FLYING_SLOW)
			Mount = mount_flying_spell_id_60;
		else if (MountType == GROUND_FAST)
			Mount = mount_spell_id_100;
		else if (MountType == GROUND_SLOW)
			Mount = mount_spell_id_60;
		if (Mount)
			me->CastSpell(me, Mount, TRIGGERED_INSTANT_CAST);
	}
}

void PartyBotAI::MountLogic(const Player* pLeader)
{
	if (pLeader->IsMounted())
	{
		MountUp(pLeader);

		if (me->GetMapId() != 530)
			return;

		bool ShouldFly = false;
		bool ShouldLand = false;
		
		const bool meIsFlying = me->IsActualFlying();
		const bool LeaderIsFlying = pLeader->IsActualFlying();

		const float LeaderGround_Z = pLeader->GetMap()->GetHeight(pLeader->GetPositionX(), pLeader->GetPositionY(), pLeader->GetPositionZ(), true);
		const float LeaderCurrent_Z = pLeader->GetPositionZ();
		const float Leader_Z_diff = LeaderCurrent_Z - LeaderGround_Z;

		const float meGround_Z = me->GetMap()->GetHeight(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), true);
		const float meCurrent_Z = me->GetPositionZ();
		const float me_Z_diff = meCurrent_Z - meGround_Z;
		
		if (LeaderIsFlying && !m_leader_has_flown)
			m_leader_has_flown = true;
		if (LeaderIsFlying && !meIsFlying && Leader_Z_diff >= 3.0f)
			ShouldFly = true;
		else if (!LeaderIsFlying && meIsFlying && Leader_Z_diff < 3.0f && me_Z_diff < 3.0f && m_leader_has_flown)
		{
			ShouldLand = true;
			m_leader_has_flown = false;
		}

		if (ShouldFly)
		{
			//me->MonsterWhisper("I SHOULD FLY", pLeader);

			WorldPacket data(SMSG_SPLINE_MOVE_SET_FLYING, 9);
			data << me->GetPackGUID();
			me->SendMessageToSet(data, true);

			if (!me->m_movementInfo.HasMovementFlag(MOVEFLAG_FLYING))
				me->m_movementInfo.AddMovementFlag(MOVEFLAG_FLYING);
			if (!me->m_movementInfo.HasMovementFlag(MOVEFLAG_FLYING2))
				me->m_movementInfo.AddMovementFlag(MOVEFLAG_FLYING2);
			if (!me->m_movementInfo.HasMovementFlag(MOVEFLAG_LEVITATING))
				me->m_movementInfo.AddMovementFlag(MOVEFLAG_LEVITATING);
			me->SendHeartBeat();
		}
		else if (ShouldLand)
		{
			//me->MonsterWhisper("I SHOULD LAND", pLeader);

			WorldPacket data(SMSG_SPLINE_MOVE_UNSET_FLYING, 9);
			data << me->GetPackGUID();
			me->SendMessageToSet(data, true);

			if (me->m_movementInfo.HasMovementFlag(MOVEFLAG_FLYING))
				me->m_movementInfo.RemoveMovementFlag(MOVEFLAG_FLYING);
			if (me->m_movementInfo.HasMovementFlag(MOVEFLAG_FLYING2))
				me->m_movementInfo.RemoveMovementFlag(MOVEFLAG_FLYING2);
			if (me->m_movementInfo.HasMovementFlag(MOVEFLAG_LEVITATING))
				me->m_movementInfo.RemoveMovementFlag(MOVEFLAG_LEVITATING);
			me->StopMoving();
			me->SendHeartBeat();
		}
	}
	// Dismount if the leader is dismounted
	else if (me->IsMounted())
		me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

	if (!pLeader->IsActualFlying() && me->IsActualFlying() && !me->IsMounted())
	{
		WorldPacket data(SMSG_SPLINE_MOVE_UNSET_FLYING, 9);
		data << me->GetPackGUID();
		me->SendMessageToSet(data, true);

		if (me->m_movementInfo.HasMovementFlag(MOVEFLAG_FLYING))
			me->m_movementInfo.RemoveMovementFlag(MOVEFLAG_FLYING);
		if (me->m_movementInfo.HasMovementFlag(MOVEFLAG_FLYING2))
			me->m_movementInfo.RemoveMovementFlag(MOVEFLAG_FLYING2);
		if (me->m_movementInfo.HasMovementFlag(MOVEFLAG_LEVITATING))
			me->m_movementInfo.RemoveMovementFlag(MOVEFLAG_LEVITATING);
		me->StopMoving();
		me->SendHeartBeat();
	}
}

bool PartyBotAI::DrinkAndEat(const Player* pLeader) const
{
	if (m_isBuffing || m_has_created_portal || me->IsInCombat() || me->IsMounted() || me->GetVictim())
		return false;

	// Cancel form to drink for mana - calculate the cost for entering form
	if (me->GetPower(POWER_MANA) < me->GetMaxPower(POWER_MANA) - me->GetCreateMana() * 60 / 100 &&
		(me->GetShapeshiftForm() == FORM_BEAR || me->GetShapeshiftForm() == FORM_DIREBEAR || me->GetShapeshiftForm() == FORM_CAT))
		me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
	
	// Check for custom follow target
	if (m_follow && m_follow->GetObjectGuid())
		pLeader = m_follow;

	if (pLeader->IsInCombat())
		return false;

	bool const needToEat = me->GetHealthPercent() < m_set_eat_threshold && (!pLeader->IsMoving() || IsStaying);
	bool const needToDrink = me->GetPowerType() == POWER_MANA && me->GetPowerPercent(POWER_MANA) < m_set_drink_threshold && (!pLeader->IsMoving() || IsStaying);

	if (!needToEat && !needToDrink)
	{
		// If I'm sitting down, probably because I was eating before, then stand up
		if (me->getStandState() != UNIT_STAND_STATE_STAND)
			me->SetStandState(UNIT_STAND_STATE_STAND);

		return false;
	}

	bool const isEating = me->HasAura(LevelAppropriateFood());
	bool const isDrinking = me->HasAura(LevelAppropriateDrink());

	if (!isEating && needToEat)
	{
		if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
		{
			if (!me->IsStopped())
				me->StopMoving();
			me->GetMotionMaster()->Clear(false);
			me->GetMotionMaster()->MoveIdle();
		}
		if (SpellEntry const* pSpellEntry = sSpellMgr.GetSpellEntry(LevelAppropriateFood()))
		{
			me->DurabilityRepairAll(false, 0, false);
			me->CastSpell(me, pSpellEntry->Id, TRIGGERED_IGNORE_COOLDOWNS);
		}
		return true;
	}

	if (!isDrinking && needToDrink)
	{
		if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
		{
			if (!me->IsStopped())
				me->StopMoving();
			me->GetMotionMaster()->Clear(false);
			me->GetMotionMaster()->MoveIdle();
		}
		if (SpellEntry const* pSpellEntry = sSpellMgr.GetSpellEntry(LevelAppropriateDrink()))
			me->CastSpell(me, pSpellEntry, TRIGGERED_IGNORE_COOLDOWNS);
		return true;
	}

	return needToEat || needToDrink;
}

bool PartyBotAI::IsBoss(Unit* pVictim)
{
	if (!pVictim || pVictim->IsPlayer() || !pVictim->IsCreature() || !pVictim->IsAlive())
		return false;

	Creature* pCreature = pVictim->ToCreature();
	if (!pCreature)
		return false;

	if (pCreature->GetCreatureInfo()->Rank == CREATURE_ELITE_WORLDBOSS ||
		pCreature->GetCreatureInfo()->Rank == CREATURE_ELITE_RAREELITE ||
		pCreature->GetCreatureInfo()->Rank == CREATURE_ELITE_RARE)
		return true;

	return BossIDs.find(pVictim->GetEntry()) != BossIDs.end();
}

Unit* PartyBotAI::GetMarkedTarget(const RaidTargetIcon mark) const
{
	if (const Group* pGroup = me->GetGroup())
	{
		const ObjectGuid targetGuid = pGroup->GetTargetWithIcon(mark);
		if (targetGuid.IsUnit())
			return me->GetMap()->GetUnit(targetGuid);
	}
	return nullptr;
}

bool PartyBotAI::UseRacials(Unit* pVictim)
{
	if (!pVictim) return false;

	switch (me->getRace())
	{
	case RACE_HUMAN:
	{
		// Perception
		if (m_spells.racial.pPerception)
		{
			for (const auto& pAttacker : me->getAttackers())
			{
				if (pAttacker->getClass() == CLASS_ROGUE && pAttacker->IsPlayer() &&
					CanTryToCastSpell(me, m_spells.racial.pPerception) &&
					DoCastSpell(me, m_spells.racial.pPerception) == SPELL_CAST_OK)
					return true;
			}
		}
		break;
	}
	case RACE_ORC:
	{
		// Blood Fury
		if (m_spells.racial.pBloodFury)
		{
			if (m_role == ROLE_MELEE_DPS && me->IsInCombat() && me->getAttackers().empty() &&
				me->CanReachWithMeleeAttack(pVictim) && IsBoss(pVictim) &&
				CanTryToCastSpell(me, m_spells.racial.pBloodFury) &&
				DoCastSpell(me, m_spells.racial.pBloodFury) == SPELL_CAST_OK)
				return true;
		}
		break;
	}
	case RACE_DWARF:
	{
		// Stoneform
		if (m_spells.racial.pStoneform)
		{
			if ((me->IsBleeding() || me->HasAuraWithDispelType(DISPEL_POISON) && !me->HasAura(24321) || me->HasAuraWithDispelType(DISPEL_DISEASE)) &&
				CanTryToCastSpell(me, m_spells.racial.pStoneform) &&
				DoCastSpell(me, m_spells.racial.pStoneform) == SPELL_CAST_OK)
				return true;
		}
		break;
	}
	case RACE_NIGHTELF:
	{
		// Shadowmeld
		if (m_spells.racial.pShadowmeld)
		{
			if (const Player* pLeader = GetPartyLeader())
			{
				if (pLeader->HasStealthAura() && !me->HasStealthAura() && !pLeader->IsMoving() && !me->IsMoving() &&
					CanTryToCastSpell(me, m_spells.racial.pShadowmeld) &&
					DoCastSpell(me, m_spells.racial.pShadowmeld) == SPELL_CAST_OK)
					return true;
			}
		}
		break;
	}
	case RACE_TAUREN:
	{
		// War Stomp
		if (m_spells.racial.pWarStomp)
		{
			if (!me->getAttackers().empty() &&
				me->GetHealthPercent() < 50.0f &&
				me->GetShapeshiftForm() == FORM_NONE &&
				me->GetEnemyCountInRadiusAround(me, 13.0f, true, true, true) &&
				me->GetEnemyCountInRadiusAround(me, 8.0f, true, true, true) >= 3)
			{
				for (const auto& pAttacker : me->getAttackers())
				{
					if (me->GetCombatDistance(pAttacker) < 8.0f &&
						!pAttacker->hasUnitState(UNIT_STAT_STUNNED) &&
						CanTryToCastSpell(me, m_spells.racial.pWarStomp) &&
						DoCastSpell(me, m_spells.racial.pWarStomp) == SPELL_CAST_OK)
						return true;
				}
			}
		}
		break;
	}
	case RACE_GNOME:
	{
		// Escape Artist
		if (m_spells.racial.pEscapeArtist)
		{
			if ((me->hasUnitState(UNIT_STAT_ROOT) && !IsStaying || me->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) && me->IsMoving()) &&
				CanTryToCastSpell(me, m_spells.racial.pEscapeArtist) &&
				DoCastSpell(me, m_spells.racial.pEscapeArtist) == SPELL_CAST_OK)
				return true;
		}
		break;
	}
	case RACE_TROLL:
	{
		// Berserking
		if (m_spells.racial.pBerserking)
		{
			if (me->IsInCombat() && me->GetHealthPercent() < 40.0f && !me->IsMoving() &&
				((m_role == ROLE_TANK || m_role == ROLE_MELEE_DPS) && me->CanReachWithMeleeAttack(pVictim) || (m_role == ROLE_HEALER || m_role == ROLE_RANGE_DPS)) &&
				CanTryToCastSpell(me, m_spells.racial.pBerserking) &&
				DoCastSpell(me, m_spells.racial.pBerserking) == SPELL_CAST_OK)
				return true;
		}
		break;
	}
	}
	return false;
}

bool PartyBotAI::AttackStart(Unit* pVictim)
{
	m_isBuffing = false;
	if (me->IsMounted())
		me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

	if (m_role == ROLE_RANGE_DPS)
	{
		// Attack from range
		if (me->Attack(pVictim, false))
		{
			if (me->getClass() == CLASS_HUNTER)
			{
				float max_range = 35.0f;
				if (me->GetLevel() >= 60 && me->HasSpell(19500)) // Hawk Eye
					max_range = 41.0f;
				me->GetMotionMaster()->MoveChase(pVictim, max_range);
			}
			else if (me->getClass() == CLASS_PRIEST)
			{
				float max_range = 20.0f;
				if (me->GetLevel() >= 60 && me->HasSpell(17325)) // Shadow Reach
					max_range = 24.0f;
				me->GetMotionMaster()->MoveChase(pVictim, max_range);
			}
			else if (me->getClass() == CLASS_SHAMAN)
			{
				float max_range = 30.0f;
				if (me->GetLevel() >= 60 && me->HasSpell(29000)) // Storm Reach
					max_range = 36.0f;
				me->GetMotionMaster()->MoveChase(pVictim, max_range);
			}
			else if (me->getClass() == CLASS_MAGE)
			{
				float max_range = 30.0f;
				if (me->GetLevel() >= 60 && me->HasSpell(16758)) // Arctic Reach
					max_range = 36.0f;
				me->GetMotionMaster()->MoveChase(pVictim, max_range);
			}
			else if (me->getClass() == CLASS_WARLOCK)
			{
				float max_range = 30.0f;
				if (me->GetLevel() >= 60 && me->HasSpell(17918)) // Destructive Reach
					max_range = 36.0f;
				me->GetMotionMaster()->MoveChase(pVictim, max_range);
			}
			else if (me->getClass() == CLASS_DRUID)
			{
				float max_range = 30.0f;
				if (me->GetLevel() >= 60 && me->HasSpell(16820)) // Nature's Reach
					max_range = 36.0f;
				me->GetMotionMaster()->MoveChase(pVictim, max_range);
			}
			else
				me->GetMotionMaster()->MoveChase(pVictim, 25.0f);
			return true;
		}

		// If for some reason you can't from range then attack from melee
		me->Attack(pVictim, true);
		me->GetMotionMaster()->MoveChase(pVictim, 1.0f, M_PI_F);
		return false;
	}

	if (me->Attack(pVictim, true))
	{
		me->GetMotionMaster()->MoveChase(pVictim, 1.0f, m_role == ROLE_MELEE_DPS ? M_PI_F : 0.0f);
		return true;
	}

	return false;
}

Unit* PartyBotAI::GetFocusedVictim(const Player* pLeader)
{
	Unit* pFocusVictim = nullptr;
	if (m_role != ROLE_TANK && (me->IsInCombat() || pLeader->GetVictim()))
	{
		if (!m_marksToFocus.empty())
		{
			for (auto const& markId : m_marksToFocus)
			{
				ObjectGuid targetGuid = me->GetGroup()->GetTargetWithIcon(markId);
				if (targetGuid && targetGuid.IsUnit())
				{
					pFocusVictim = me->GetMap()->GetUnit(targetGuid);
					if (pFocusVictim &&
						pFocusVictim->IsInCombat() &&
						IsValidHostileTarget(pFocusVictim) &&
						me->IsWithinDist(pFocusVictim, 50.0f) &&
						me->IsWithinLOSInMap(pFocusVictim))
						AttackStart(pFocusVictim);
					else pFocusVictim = nullptr;
				}
			}
		}
	}
	return pFocusVictim;
}

bool PartyBotAI::IsBlacklisted(const std::string& SpellName) const
{
	if (!m_spellBlacklist.empty())
		for (const auto& itr : m_spellBlacklist)
			if (itr == SpellName)
				return true;

	return false;
}

Unit* PartyBotAI::SelectClosestAttacker() const
{
	Group* pGroup = me->GetGroup();
	if (!pGroup)
		return nullptr;

	float ClosestDistance = 100.0f;
	Unit* ClosestAttacker = nullptr;

	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (const Player* pMember = itr->getSource())
		{
			if (pMember->getAttackers().empty())
				continue;

			for (auto const& eachAttacker : pMember->getAttackers())
			{
				if (eachAttacker &&
					IsValidHostileTarget(eachAttacker) &&
					me->GetCombatDistance(eachAttacker) < ClosestDistance)
				{
					ClosestDistance = me->GetCombatDistance(eachAttacker);
					ClosestAttacker = eachAttacker;
				}
			}
		}
	}

	return ClosestAttacker;
}

Unit* PartyBotAI::SelectHighestHealthAttacker(const bool CheckDeadzone) const
{
	Group* pGroup = me->GetGroup();
	if (!pGroup)
		return nullptr;

	uint32 HighestHealth = 0;
	Unit* HighestHealthAttacker = nullptr;

	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (const Player* pMember = itr->getSource())
		{
			if (pMember->getAttackers().empty())
				continue;

			for (auto const& eachAttacker : pMember->getAttackers())
			{
				if (eachAttacker &&
					eachAttacker->IsAlive() &&
					IsValidHostileTarget(eachAttacker) &&
					me->IsWithinDist(eachAttacker, 50.0f) &&
					me->IsWithinLOSInMap(eachAttacker))
				{
					if (CheckDeadzone)
						if (me->GetCombatDistance(eachAttacker) < 8.0f)
							continue;

					if (eachAttacker->GetHealth() > HighestHealth)
					{
						HighestHealth = eachAttacker->GetHealth();
						HighestHealthAttacker = eachAttacker;
					}
				}
			}
		}
	}

	return HighestHealthAttacker;
}

Unit* PartyBotAI::SelectLowestHealthAttacker(const bool CheckDeadzone) const
{
	Group* pGroup = me->GetGroup();

	if (!pGroup)
		return nullptr;

	uint32 LowestHealth = MAXUINT32;
	Unit* LowestHealthAttacker = nullptr;

	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (const Player* pMember = itr->getSource())
		{
			if (pMember->getAttackers().empty())
				continue;

			for (auto const& eachAttacker : pMember->getAttackers())
			{
				if (eachAttacker &&
					eachAttacker->IsAlive() &&
					IsValidHostileTarget(eachAttacker) &&
					me->IsWithinDist(eachAttacker, 50.0f) &&
					me->IsWithinLOSInMap(eachAttacker))
				{
					if (CheckDeadzone)
						if (me->GetCombatDistance(eachAttacker) < 8.0f)
							continue;

					if (eachAttacker->GetHealth() < LowestHealth)
					{
						LowestHealth = eachAttacker->GetHealth();
						LowestHealthAttacker = eachAttacker;
					}
				}
			}
		}
	}

	if (LowestHealthAttacker)
		return LowestHealthAttacker;

	return nullptr;
}

Unit* PartyBotAI::SelectPartyAttackTarget() const
{
	Group* pGroup = me->GetGroup();
	if (!pGroup)
		return nullptr;

	float distance = 50.0f;
	Unit* ClosestAttacker = nullptr;

	float distancePet = 50.0f;
	Unit* ClosestAttackerPet = nullptr;

	// Prioritize gettings adds of the healer
	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (Player* pMember = itr->getSource())
		{
			if (pMember == me)
				continue;

			if (pMember->getAttackers().empty())
				continue;

			if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI()))
			{
				if (pAI->m_role == ROLE_HEALER)
				{
					for (const auto pAttacker : pMember->getAttackers())
					{
						if (IsValidHostileTarget(pAttacker) &&
							me->IsWithinDist(pAttacker, 50.0f) &&
							pAttacker->GetDistance(pMember) < distance)
						{
							distance = pAttacker->GetDistance(pMember);
							ClosestAttacker = pAttacker;
						}
					}
				}
			}
		}
	}

	if (ClosestAttacker)
		return ClosestAttacker;

	// Take care of the rest of the group
	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (Player* pMember = itr->getSource())
		{
			if (pMember == me)
				continue;

			if (!pMember->getAttackers().empty())
			{
				// Ignore another tank's attackers
				if (IsTank(pMember))
					continue;

				for (const auto pAttacker : pMember->getAttackers())
				{
					if (IsValidHostileTarget(pAttacker) &&
						me->IsWithinDist(pAttacker, 50.0f) &&
						pAttacker->GetDistance(pMember) < distance)
					{
						distance = pAttacker->GetDistance(pMember);
						ClosestAttacker = pAttacker;
					}
				}
			}

			if (const Pet* pPet = pMember->GetPet())
			{
				if (!pPet->getAttackers().empty())
				{
					for (const auto pAttacker : pPet->getAttackers())
					{
						if (IsValidHostileTarget(pAttacker) &&
							me->IsWithinDist(pAttacker, 50.0f) &&
							me->IsWithinLOSInMap(pAttacker) &&
							pAttacker->GetDistance(pMember) < distancePet)
						{
							distancePet = pAttacker->GetDistance(pMember);
							ClosestAttackerPet = pAttacker;
						}
					}
				}
			}
		}
	}

	if (ClosestAttacker)
		return ClosestAttacker;

	if (ClosestAttackerPet)
		return ClosestAttackerPet;

	return nullptr;
}

Unit* PartyBotAI::SelectAttackTarget(const Player* pLeader) const
{
	// On Whisper priority
	if (!OnWhisper_CastSpellTargetGUID.IsEmpty())
	{
		if (Unit* pTarget = me->GetMap()->GetUnit(OnWhisper_CastSpellTargetGUID))
		{
			if (IsValidHostileTarget(pTarget) &&
				me->IsWithinDist(pTarget, 50.0f) &&
				me->IsWithinLOSInMap(pTarget))
				return pTarget;
		}
	}

	// Focus mark priority
	if (me->IsInCombat() || pLeader->GetVictim())
	{
		if (!m_marksToFocus.empty())
		{
			for (auto const& markId : m_marksToFocus)
			{
				ObjectGuid targetGuid = me->GetGroup()->GetTargetWithIcon(markId);
				if (targetGuid && targetGuid.IsUnit())
					if (Unit* pVictim = me->GetMap()->GetUnit(targetGuid))
						if (pVictim->IsInCombat() &&
							IsValidHostileTarget(pVictim) &&
							me->IsWithinDist(pVictim, 50.0f) &&
							me->IsWithinLOSInMap(pVictim))
							return pVictim;
			}
		}
	}

	if (m_targeting_type == TARGETING_INDEPENDENT)
	{
		// Go after the closest enemy when in stealth
		if (me->HasAuraType(SPELL_AURA_MOD_STEALTH))
			if (Unit* pClosestAttacker = SelectClosestAttacker())
				return pClosestAttacker;

		if (m_role == ROLE_RANGE_DPS)
		{
			// Ranged DPS attack the highest health attacker
			if (me->getClass() == CLASS_HUNTER)
				if (Unit* pHighestHealthAttacker = SelectHighestHealthAttacker(true))
					return pHighestHealthAttacker;

			if (Unit* pHighestHealthAttacker = SelectHighestHealthAttacker())
				return pHighestHealthAttacker;
		}
		else
		{
			// Melee DPS attack the lowest health attacker
			if (Unit* pLowestHealthAttacker = SelectLowestHealthAttacker())
				return pLowestHealthAttacker;
		}
	}

	// Who is the leader attacking.
	if (Unit* pVictim = pLeader->GetVictim())
		if (IsValidHostileTarget(pVictim))
			return pVictim;

	// Go after the lowest health attacker
	if (m_targeting_type == TARGETING_FOCUSED)
	{
		if (Unit* pLowestHealthAttacker = SelectLowestHealthAttacker())
			return pLowestHealthAttacker;
	}

	// Who is attacking the Companion
	for (const auto pAttacker : me->getAttackers())
	{
		if (IsValidHostileTarget(pAttacker) &&
			me->IsWithinDist(pAttacker, 50.0f) &&
			me->IsWithinLOSInMap(pAttacker))
			return pAttacker;
	}

	// Check if other group members are under attack
	if (Unit* pPartyAttacker = SelectPartyAttackTarget())
		return pPartyAttacker;

	// Assist pet if it's in combat
	if (Pet* pPet = me->GetPet())
		if (Unit* pPetAttacker = pPet->getAttackerForHelper())
			return pPetAttacker;

	return nullptr;
}

void PartyBotAI::TargetAcquisition(const Player* pLeader)
{
	// Healers don't attack
	if (m_role == ROLE_HEALER)
		return;

	// Check if the Bot is attacking something
	Unit* pVictim = me->GetVictim();

	// Prioritize marked targets in combat
	Unit* pFocusedVictim = GetFocusedVictim(pLeader);
	if (pFocusedVictim)
		pVictim = pFocusedVictim;

	// If I don't have a victim or the victim is dead or it has CC that can be interrupted then select an attack target
	if (!pVictim || !IsValidHostileTarget(pVictim) || m_role == ROLE_RANGE_DPS && !pFocusedVictim && m_targeting_type == TARGETING_INDEPENDENT)
	{
		if (pVictim && !IsValidHostileTarget(pVictim))
			me->AttackStop(true);

		pVictim = SelectAttackTarget(pLeader); // Victim Selection - IMPORTANT! - Local Scope!
		if (pVictim)
		{
			// If I'm in stealth then position behind target but don't attack
			if (!IsStaying && (me->getClass() == CLASS_DRUID && m_role == ROLE_MELEE_DPS && m_spells.druid.pProwl && me->IsSpellReady(m_spells.druid.pProwl->Id) && !IsBlacklisted("Prowl") || me->getClass() == CLASS_ROGUE && m_spells.rogue.pStealth && me->IsSpellReady(m_spells.rogue.pStealth->Id) && !IsBlacklisted("Stealth")) && (!me->IsInCombat() || me->HasAuraType(SPELL_AURA_MOD_STEALTH)) && me->CanFreeMove())
			{
				// Position behind the victim
				if (!(me->IsBehindTarget(pVictim) && me->CanReachWithMeleeAttack(pVictim)) && !me->HasAuraType(SPELL_AURA_MOD_ROOT) && !IsStaying && me->CanFreeMove())
				{
					float x, y, z;
					pVictim->GetRelativePositions(-1.0f, 0.0f, 0.0f, x, y, z);
					me->GetMotionMaster()->MovePoint(1, x, y, z, FORCED_MOVEMENT_RUN);
					me->SetInFront(pVictim);
				}
				UpdateOutOfCombatAI();
				return;
			}
			// Start attacking the victim and chase it
			if (me->GetVictim() != pVictim)
			{
				if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
					me->GetMotionMaster()->Clear();
				if (AttackStart(pVictim)) // Confirm Victim Selection - IMPORTANT! - Victim Selection is lost if not confirmed!
					return;
			}
		}
	}
}

bool PartyBotAI::FindPathToTarget(const Unit* pUnit, const Unit* pTarget)
{
	const float Angle = pTarget->GetAngle(pUnit);
	float x, y, z;
	pTarget->GetNearPoint(pUnit, x, y, z, pUnit->GetObjectBoundingRadius(), 0.0f, Angle, pTarget->IsInWater());

	PathFinder Pathfinder(pUnit);
	Pathfinder.calculate(x, y, z);
	if (Pathfinder.getPathType() & PATHFIND_NOPATH)
		return false;

	const float CollisionHeight = pUnit->GetCollisionHeight();
	const auto& Path = Pathfinder.getPath();
	for (size_t i = 1; i < Path.size(); ++i)
	{
		const G3D::Vector3& dataPrevious = Path.at(i - 1);
		const G3D::Vector3& data = Path.at(i);
		if (!pUnit->GetMap()->IsInLineOfSight(dataPrevious.x, dataPrevious.y, dataPrevious.z + CollisionHeight, data.x, data.y, data.z + CollisionHeight, true))
			return false;
	}

	return true;
}

bool PartyBotAI::OnWhisperComeToggle(Player*& pLeader)
{
	if (!m_toggle_come || IsStaying)
		return false;

	if (me->CanFreeMove() && !me->HasAuraType(SPELL_AURA_MOD_ROOT) && me->GetDistance(pLeader) > 0.0f && me->GetDistance(pLeader) <= 200.0f)
	{
		// Remove temporary root
		if (me->hasUnitState(UNIT_STAT_ROOT))
			me->clearUnitState(UNIT_STAT_ROOT);

		// Stop attacking
		if (me->GetVictim())
		{
			me->AttackStop();
			me->InterruptNonMeleeSpells(false);
		}

		// Stop pet attacking
		if (Pet* pPet = me->GetPet())
		{
			if (pPet->GetVictim())
			{
				pPet->AttackStop();
				pPet->InterruptNonMeleeSpells(false);

				// Set pet on PASSIVE
				WorldPacket data(CMSG_PET_ACTION);
				data << pPet->GetGUIDLow();
				data << 100663296;
				data << ObjectGuid();
				me->GetSession()->HandlePetAction(data);
			}
		}

		// Mount logic
		MountLogic(pLeader);

		// Teleport to leader if he's fallen
		if (pLeader->IsFalling() && pLeader->GetFallingDistance() >= 10.0f)
			m_leader_has_fallen = true;
		if (!pLeader->IsFalling() && m_leader_has_fallen)
			if (Teleport(pLeader))
				return true;

		// Move to leader's location
		me->GetMotionMaster()->MovePoint(1, pLeader->GetPositionX(), pLeader->GetPositionY(), pLeader->GetPositionZ(), FORCED_MOVEMENT_RUN);

		// Cast instant spells
		//if (CastOnTheGo())
		//	return true;

		return true;
	}

	// While stacked on the leader, root to prevent movement
	if (me->GetDistance(pLeader) == 0.0f && !me->hasUnitState(UNIT_STAT_ROOT))
		me->addUnitState(UNIT_STAT_ROOT);

	return false;
}

bool PartyBotAI::OnWhisperCome(Player*& pLeader)
{
	if (!m_come_location || IsStaying)
		return false;

	const float distance = me->GetDistance(m_come_location_x, m_come_location_y, m_come_location_z);
	if (distance > 0.0f && distance <= 200.0f)
	{
		// Stop attacking
		if (me->GetVictim())
		{
			me->AttackStop();
			me->InterruptNonMeleeSpells(false);
		}

		// Stop pet attacking
		if (Pet* pPet = me->GetPet())
		{
			if (pPet->GetVictim())
			{
				pPet->AttackStop();
				pPet->InterruptNonMeleeSpells(false);

				// Set pet on PASSIVE
				WorldPacket data(CMSG_PET_ACTION);
				data << pPet->GetGUIDLow();
				data << 100663296;
				data << ObjectGuid();
				me->GetSession()->HandlePetAction(data);
			}
		}

		// Mount logic
		MountLogic(pLeader);

		// Move to assigned coordinates
		if (!IsStaying && me->CanFreeMove() && !me->HasAuraType(SPELL_AURA_MOD_ROOT))
			me->GetMotionMaster()->MovePoint(1, m_come_location_x, m_come_location_y, m_come_location_z, FORCED_MOVEMENT_RUN);

		// Cast instant spells
		//if (CastOnTheGo())
			//return;

		//return;

		m_come_location = false;
		m_come_location_x = 0.0f;
		m_come_location_y = 0.0f;
		m_come_location_z = 0.0f;
	}

	return false;
}

bool PartyBotAI::OnWhisperLogic(Player*& pLeader)
{
	// Change the leader for .z follow
	if (m_follow)
		pLeader = m_follow;

	// Come Toggle
	if (OnWhisperComeToggle(pLeader))
		return true;

	// Come Location
	if (OnWhisperCome(pLeader))
		return true;

	// Follow passively
	if (IsPassive)
	{
		if (me->GetVictim())
		{
			me->AttackStop();
			me->InterruptNonMeleeSpells(false);
		}

		if (Pet* pPet = me->GetPet())
		{
			if (pPet->GetVictim())
			{
				pPet->AttackStop();
				pPet->InterruptNonMeleeSpells(false);

				// Set pet on PASSIVE
				WorldPacket data(CMSG_PET_ACTION);
				data << pPet->GetGUIDLow();
				data << 100663296;
				data << ObjectGuid();
				me->GetSession()->HandlePetAction(data);
			}
		}

		// Mount Logic
		MountLogic(pLeader);

		// If I'm not moving...
		if (!IsStaying && !m_toggle_come && !m_DND && me->IsWithinDistInMap(pLeader, 100.0f) && !me->IsMoving() && !pLeader->IsDead() && me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
		{
			Follow(pLeader);
		}

		//if (CastOnTheGo())
		//	return;

		//return;
	}

	return false;
}

void PartyBotAI::MovementSync(const uint32 diff)
{
	if (!me->IsMoving())
		return;

	MSYNC_Timer.Update(diff);
	if (MSYNC_Timer.Passed())
		MSYNC_Timer.Reset(1 * IN_MILLISECONDS);
	else return;

	const float distance = me->GetDistance(MSYNC_X, MSYNC_Y, MSYNC_Z);
	if (distance == 0.0f)
	{
		if (!MSYNC_CHECK)
		{
			MSYNC_CHECK = true;
			return;
		}
		me->StopMoving(true);
		MSYNC_CHECK = false;
	}
	else
	{
		MSYNC_X = me->GetPositionX();
		MSYNC_Y = me->GetPositionY();
		MSYNC_Z = me->GetPositionZ();
	}
}

Player* PartyBotAI::SelectResurrectionTarget() const
{
	if (Group* pGroup = me->GetGroup())
	{
		// Revive players first
		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (!pMember->IsAlive() && pMember->ToWorldObject() && me->IsWithinLOSInMap(pMember->ToWorldObject()) && me->IsWithinDist(pMember->ToWorldObject(), 30.0f))
				{
					// Can't resurrect self.
					if (pMember == me)
						continue;

					if (pMember->AI())
						continue;

					if (AreOthersOnSameTarget(pMember->GetObjectGuid(), false, true))
						continue;

					if (pMember->GetDeathState() == CORPSE)
						return pMember;
				}
			}
		}

		// Revive AI Healers first
		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (!pMember->IsAlive() && pMember->ToWorldObject() && me->IsWithinLOSInMap(pMember->ToWorldObject()) && me->IsWithinDist(pMember->ToWorldObject(), 30.0f))
				{
					// Can't resurrect self.
					if (pMember == me)
						continue;

					if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI()))
					{
						if (pAI->m_role == ROLE_HEALER)
						{
							if (AreOthersOnSameTarget(pMember->GetObjectGuid(), false, true))
								continue;

							if (pMember->GetDeathState() == CORPSE)
								return pMember;
						}
					}
				}
			}
		}

		// Revive the rest
		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (!pMember->IsAlive() && pMember->ToWorldObject() && me->IsWithinLOSInMap(pMember->ToWorldObject()) && me->IsWithinDist(pMember->ToWorldObject(), 30.0f))
				{
					// Can't resurrect self.
					if (pMember == me)
						continue;

					if (AreOthersOnSameTarget(pMember->GetObjectGuid(), false, true))
						continue;

					if (pMember->GetDeathState() == CORPSE)
						return pMember;
				}
			}
		}
	}

	return nullptr;
}

void PartyBotAI::UpdateOutOfCombatAI()
{
	if (me->IsInCombat() || me->IsMounted())
		return;

	// Revive Group Members
	if (m_resurrectionSpell)
		if (Player* pTarget = SelectResurrectionTarget())
			if (CanTryToCastSpell(pTarget, m_resurrectionSpell))
				if (DoCastSpell(pTarget, m_resurrectionSpell) == SPELL_CAST_OK)
					return;

	// If I was buffing then wait and see if I still need to buff more
	//if (m_isBuffing)
	//	return;

	switch (me->getClass())
	{
	case CLASS_WARRIOR:
		UpdateOutOfCombat_Warrior();
		break;
	case CLASS_PALADIN:
		UpdateOutOfCombat_Paladin();
		break;
	case CLASS_HUNTER:
		UpdateOutOfCombat_Hunter();
		break;
	case CLASS_ROGUE:
		UpdateOutOfCombat_Rogue();
		break;
	case CLASS_PRIEST:
		UpdateOutOfCombat_Priest();
		break;
	case CLASS_SHAMAN:
		UpdateOutOfCombat_Shaman();
		break;
	case CLASS_MAGE:
		UpdateOutOfCombat_Mage();
		break;
	case CLASS_WARLOCK:
		UpdateOutOfCombat_Warlock();
		break;
	case CLASS_DRUID:
		UpdateOutOfCombat_Druid();
		break;
	}
}

void PartyBotAI::UpdateInCombatAI()
{
	// Check if I'm attacking something
	Unit* pVictim = me->GetVictim();

	// Use Racial spells
	if (UseRacials(pVictim))
		return;

	switch (me->getClass())
	{
	case CLASS_WARRIOR:
		UpdateInCombat_Warrior();
		break;
	case CLASS_PALADIN:
		UpdateInCombat_Paladin();
		break;
	case CLASS_HUNTER:
		UpdateInCombat_Hunter();
		break;
	case CLASS_ROGUE:
		UpdateInCombat_Rogue();
		break;
	case CLASS_PRIEST:
		UpdateInCombat_Priest();
		break;
	case CLASS_SHAMAN:
		UpdateInCombat_Shaman();
		break;
	case CLASS_MAGE:
		UpdateInCombat_Mage();
		break;
	case CLASS_WARLOCK:
		UpdateInCombat_Warlock();
		break;
	case CLASS_DRUID:
		UpdateInCombat_Druid();
		break;
	}
}

bool PartyBotAI::UseHealingPotion() const
{
	bool result = false;

	if (!me)
	{
		return false;
	}
	if (!me->IsInCombat())
	{
		return false;
	}
	uint32 itemEntry;
	if (me->GetLevel() >= 50)
	{
		itemEntry = 18253;
	}
	else if (me->GetLevel() >= 45)
	{
		itemEntry = 13446;
	}
	else if (me->GetLevel() >= 35)
	{
		itemEntry = 3928;
	}
	else if (me->GetLevel() >= 21)
	{
		itemEntry = 1710;
	}
	else if (me->GetLevel() >= 12)
	{
		itemEntry = 929;
	}
	else
	{
		itemEntry = 118;
	}
	if (!me->HasItemCount(itemEntry, 1))
	{
		me->StoreNewItemInBestSlots(itemEntry, 5);
	}
	Item* pItem = GetItemInInventory(itemEntry);
	if (pItem && !pItem->IsInTrade())
	{
		if (UseItem(pItem, me))
		{
			/*Player* pLeader = GetPartyLeader();
			std::string chatResponse = "Used ";
			chatResponse += pItem->GetProto()->Name1;
			me->MonsterWhisper(chatResponse.c_str(), pLeader);*/

			result = true;
		}
	}

	return result;
}

void PartyBotAI::UpdateAI(uint32 const diff)
{
	// If the Bot can't find his Leader then despawn him
	Player* pLeader = GetPartyLeader();
	if (m_initialized && !pLeader)
	{
		GetBotEntry()->SetRequestRemoval(true);
		return;
	}

	// Don't execute logic if the Bot needs to be despawned
	if (GetBotEntry()->GetRequestRemoval() || !me->IsInWorld() || me->IsBeingTeleported())
		return;

	// Clear settings if Bot is dead
	if (ClearSettingsWhenDead())
		return;
	
	// Remove the moving flag
	MovementSync(diff);

	// Bot update interval
	m_updateTimer.Update(diff);
	if (m_updateTimer.Passed())
		m_updateTimer.Reset(sPlayerBotMgr.BOT_UPDATE_INTERVAL);
	else return;

	// Initialize Bot - one time only
	if (InitializeBot())
		return;

	// Stop healing if target is full health
	if (Spell* pSpellGeneric = me->GetCurrentSpell(CURRENT_GENERIC_SPELL))
	{
		if ((pSpellGeneric->m_spellInfo->SpellName[0] == "Flash Heal" ||
			pSpellGeneric->m_spellInfo->SpellName[0] == "Greater Heal" ||
			pSpellGeneric->m_spellInfo->SpellName[0] == "Heal" ||
			pSpellGeneric->m_spellInfo->SpellName[0] == "Holy Light" ||
			pSpellGeneric->m_spellInfo->SpellName[0] == "Flash of Light" ||
			pSpellGeneric->m_spellInfo->SpellName[0] == "Healing Touch" ||
			pSpellGeneric->m_spellInfo->SpellName[0] == "Regrowth" ||
			pSpellGeneric->m_spellInfo->SpellName[0] == "Healing Wave" ||
			pSpellGeneric->m_spellInfo->SpellName[0] == "Lesser Healing Wave" ||
			pSpellGeneric->m_spellInfo->SpellName[0] == "Chain Heal") &&
			pSpellGeneric->getState() != SPELL_STATE_FINISHED)
		{
			if (Unit* pHealedTarget = ObjectAccessor::FindPlayer(me->GetTargetGuid()))
			{
				if (pHealedTarget->GetHealthPercent() >= 100.0f)
				{
					/*std::string chatResponse = "STOP HEALING! ";
					chatResponse += me->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0];
					me->Say(chatResponse, LANG_UNIVERSAL);*/

					me->InterruptNonMeleeSpells(true);
				}
				else if (me->GetLevel() - pHealedTarget->GetLevel() <= 10)
				{
					const auto HealingDone = static_cast<float>(GetHealingDoneBySpell(pSpellGeneric->m_spellInfo, pHealedTarget));
					const auto MissingHealth = static_cast<float>(pHealedTarget->GetMaxHealth() - pHealedTarget->GetHealth());
					const float PercentHealed = MissingHealth / HealingDone * 100.0f;
					if (PercentHealed < 75.0f)
					{
						/*std::string chatResponse = "DON'T OVERHEAL! ";
						chatResponse += std::to_string(PercentHealed) + " ";
						chatResponse += me->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0];
						chatResponse += " " + ChatHandler(me).playerLink(pHealedTarget->GetName());
						me->Say(chatResponse, LANG_UNIVERSAL);*/

						me->InterruptNonMeleeSpells(true);
					}
				}
			}
		}
	}
	
	// Prioritize executing commands logic
	if (OnWhisperLogic(pLeader))
		return;

	// Don't do anything if I'm currently casting or channeling a spell
	if (me->IsNonMeleeSpellCasted(false, false, true))
		return;

	// Teleport to leader if too far away
	if (Teleport(pLeader))
		return;

	// Follow the Leader
	if (Follow(pLeader))
		return;

	// Check if I need to eat or drink
	if (DrinkAndEat(pLeader))
		return;

	// Mount Logic
	MountLogic(pLeader);

	// Start attacking
	TargetAcquisition(pLeader);

	return;

	// Switch between combat and out of combat strategies
	if (me->IsInCombat())
		UpdateInCombatAI();
	else
		UpdateOutOfCombatAI();
}