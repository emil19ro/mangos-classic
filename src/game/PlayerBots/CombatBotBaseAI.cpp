#include "CombatBotBaseAI.h"
#include "PartyBotAI.h"
#include "PlayerBotMgr.h"
#include "Entities/ItemEnchantmentMgr.h"

/*********************************************************/
/***               BOT INITIALIZATION                  ***/
/*********************************************************/

void CombatBotBaseAI::OnPacketReceived(WorldPacket const& packet)
{
	switch (packet.GetOpcode())
	{
	case SMSG_RESURRECT_REQUEST:
	{
		if (!me->IsAlive())
		{
			// 8 + 1 is packet length, uint8 == 1
			WorldPacket data(CMSG_RESURRECT_RESPONSE, 8 + 1);
			data << me->GetResurrector();
			data << static_cast<uint8>(1);
			me->GetSession()->HandleResurrectResponseOpcode(data);
		}
		break;
	}
	case SMSG_GROUP_SET_LEADER:
	{
		WorldPacket p(packet);
		std::string name;
		p >> name;
		if (me->GetGroup() && name == me->GetName())
		{
			if (const auto pAI = dynamic_cast<PartyBotAI*>(me->AI_NYCTERMOON()))
			{
				if (Player* originalLeader = ObjectAccessor::FindPlayerNotInWorld(pAI->m_leaderGUID))
				{
					if (me->GetGroup()->IsMember(originalLeader->GetObjectGuid()))
					{
						p << originalLeader->GetObjectGuid();
						me->GetSession()->HandleGroupSetLeaderOpcode(p);
					}
				}
			}
		}
		break;
	}
	case SMSG_DUEL_REQUESTED:
	{
		WorldPacket data(CMSG_DUEL_ACCEPTED, 8);
		data << me->GetObjectGuid();
		me->GetSession()->HandleDuelAcceptedOpcode(data);
		break;
	}
	case SMSG_TRADE_STATUS:
	{
		auto status = packet.contents();
		if (*status == TRADE_STATUS_BEGIN_TRADE)
		{
			WorldPacket data(CMSG_BEGIN_TRADE);
			me->GetSession()->HandleBeginTradeOpcode(data);
			if (const auto pAI = dynamic_cast<PartyBotAI*>(me->AI_NYCTERMOON()))
			{
				// Mage food/drink trade
				if (me->getClass() == CLASS_MAGE)
				{
					if (!me->HasItemCount(pAI->TradeConjuredFood(), 1))
						me->StoreNewItemInBestSlots(pAI->TradeConjuredFood(), 20);
					if (!me->HasItemCount(pAI->TradeConjuredWater(), 1))
						me->StoreNewItemInBestSlots(pAI->TradeConjuredWater(), 20);
					TradeData* MyTrade = me->GetTradeData();
					if (MyTrade && me->GetTrader())
					{
						// Trade food
						if (Item* pItem = pAI->GetItemInInventory(pAI->TradeConjuredFood()))
						{
							if (!MyTrade->GetItem(static_cast<TradeSlots>(0)))
							{
								MyTrade->SetItem(static_cast<TradeSlots>(0), pItem);
								pAI->Emote(COMPANIONS_EMOTE_YOURE_WELCOME, me->GetTrader());
							}
						}
						// Trade drink to mana users
						if (me->GetTrader()->GetPowerType() == POWER_MANA)
						{
							if (Item* pItem = pAI->GetItemInInventory(pAI->TradeConjuredWater()))
							{
								if (!MyTrade->GetItem(static_cast<TradeSlots>(1)))
								{
									MyTrade->SetItem(static_cast<TradeSlots>(1), pItem);
									pAI->Emote(COMPANIONS_EMOTE_YOURE_WELCOME, me->GetTrader());
								}
							}
						}
					}
				}
				// Warlock Healthstone trade
				if (me->getClass() == CLASS_WARLOCK)
				{
					if (me->GetLevel() >= 10)
					{
						if (!me->HasItemCount(pAI->TradeHealthstone(), 1))
							me->StoreNewItemInBestSlots(pAI->TradeHealthstone(), 1);
						// Trade Healthstone
						TradeData* MyTrade = me->GetTradeData();
						if (MyTrade && me->GetTrader())
						{
							if (Item* pItem = pAI->GetItemInInventory(pAI->TradeHealthstone()))
							{
								if (!MyTrade->GetItem(static_cast<TradeSlots>(0)))
								{
									MyTrade->SetItem(static_cast<TradeSlots>(0), pItem);
									pAI->Emote(COMPANIONS_EMOTE_YOURE_WELCOME, me->GetTrader());
								}
							}
						}
					}
				}
				break;
			}
		}
		else if (*status == TRADE_STATUS_TRADE_ACCEPT)
		{
			TradeData* MyTrade = me->GetTradeData();
			if (!MyTrade)
				return;
			Player* Trader = MyTrade->GetTrader();
			if (!Trader)
				return;
			TradeData* HisTrade = Trader->GetTradeData();
			if (!HisTrade)
				return;
			bool cancel = false;

			for (uint8 i = 0; i < TRADE_SLOT_COUNT; ++i)
			{
				if (Item* pItem = HisTrade->GetItem(static_cast<TradeSlots>(i)))
				{
					if ((strstr(pItem->GetProto()->Name1, "Lockbox") || strstr(pItem->GetProto()->Name1, "Locked Chest") || strstr(pItem->GetProto()->Name1, "Thaurissan Family Jewels")) && me->getClass() == CLASS_ROGUE)
						continue;

					if (strstr(pItem->GetProto()->Name1, "Healthstone"))
						continue;

					if (pItem->GetProto()->Class == ITEM_CLASS_CONSUMABLE)
						continue;

					cancel = true;
					break;
				}
			}

			if (cancel)
			{
				// Emote No
				WorldPacket data(SMSG_TEXT_EMOTE);
				data << TEXTEMOTE_NO;
				data << 1;
				data << Trader->GetObjectGuid();
				me->GetSession()->HandleTextEmoteOpcode(data);

				// Cancel Trade
				WorldPacket data_cancel(CMSG_CANCEL_TRADE);
				me->GetSession()->HandleCancelTradeOpcode(data_cancel);

				const std::string chatResponse = "Sorry, I don't want your items.";
				me->MonsterWhisper(chatResponse.c_str(), Trader);
				break;
			}

			WorldPacket data(CMSG_ACCEPT_TRADE, 4);
			data << static_cast<uint32>(1);
			me->GetSession()->HandleAcceptTradeOpcode(data);
			break;
		}
		else if (*status == TRADE_STATUS_TRADE_COMPLETE)
		{
			for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
			{
				Item* pItem = me->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
				if (pItem && !pItem->IsEquipped() && pItem->GetProto()->Class == ITEM_CLASS_CONSUMABLE)
				{
					SpellCastTargets targets;
					targets.setUnitTarget(me);
					me->CastItemUseSpell(pItem, targets);
					break;
				}
			}
		}
		break;
	}
	case SMSG_NEW_WORLD:
	{
		if (me->IsBeingTeleportedFar())
			me->GetSession()->HandleMoveWorldportAckOpcode();
		break;
	}
	case MSG_MOVE_TELEPORT_ACK:
	{
		if (me->IsBeingTeleportedNear())
		{
			WorldPacket rp(packet);
			ObjectGuid guid;
			rp >> guid.ReadAsPacked();
			if (guid != me->GetObjectGuid()) return;
			uint32 counter;
			rp >> counter;
			WorldPacket data(MSG_MOVE_TELEPORT_ACK, 8 + 4 + 4);
			data << me->GetObjectGuid();
			data << counter;
			data << static_cast<uint32>(time(nullptr));
			me->GetSession()->HandleMoveTeleportAckOpcode(data);
		}
		break;
	}
	case MSG_RAID_READY_CHECK:
	{
		GetBotEntry()->SetMailBox(packet);
		break;
	}
	}
}

void CombatBotBaseAI::ProcessPacket(WorldPacket const& packet)
{
	switch (packet.GetOpcode())
	{
	case MSG_RAID_READY_CHECK: // OK
	{
		WorldPacket data(MSG_RAID_READY_CHECK, 1);
		data << 1;
		me->GetSession()->HandleRaidReadyCheckOpcode(data);
		break;
	}
	}
}

void CombatBotBaseAI::AutoAssignRole()
{
	switch (me->getClass())
	{
	case CLASS_WARRIOR:
	case CLASS_PALADIN:
	case CLASS_ROGUE:
	{
		m_role = ROLE_MELEE_DPS;
		return;
	}
	case CLASS_HUNTER:
	case CLASS_MAGE:
	case CLASS_WARLOCK:
	case CLASS_PRIEST:
	case CLASS_SHAMAN:
	case CLASS_DRUID:
	{
		m_role = ROLE_RANGE_DPS;
		return;
	}
	}
}

void CombatBotBaseAI::ResetSpellData()
{
	for (auto& ptr : m_spells.raw.spells)
		ptr = nullptr;

	m_resurrectionSpell = nullptr;
	spellListTaunt.clear();
}

void CombatBotBaseAI::PopulateSpellData()
{
	// Air Totems
	SpellEntry const* pGraceOfAirTotem = nullptr;
	SpellEntry const* pNatureResistanceTotem = nullptr;
	SpellEntry const* pWindfuryTotem = nullptr;
	SpellEntry const* pWindwallTotem = nullptr;
	SpellEntry const* pTranquilAirTotem = nullptr;

	// Earth Totems
	SpellEntry const* pEarthbindTotem = nullptr;
	SpellEntry const* pStoneclawtotem = nullptr;
	SpellEntry const* pStoneskinTotem = nullptr;
	SpellEntry const* pStrengthOfEarthTotem = nullptr;
	SpellEntry const* pTremorTotem = nullptr;

	// Fire Totems
	SpellEntry const* pFireNovaTotem = nullptr;
	SpellEntry const* pMagmaTotem = nullptr;
	SpellEntry const* pSearingTotem = nullptr;
	SpellEntry const* pFlametongueTotem = nullptr;
	SpellEntry const* pFrostResistanceTotem = nullptr;

	// Water Totems
	SpellEntry const* pFireResistanceTotem = nullptr;
	SpellEntry const* pDiseaseCleansingTotem = nullptr;
	SpellEntry const* pHealingStreamTotem = nullptr;
	SpellEntry const* pManaSpringTotem = nullptr;
	SpellEntry const* pPoisonCleansingTotem = nullptr;

	// Shaman Weapon Buffs
	SpellEntry const* pFrostbrandWeapon = nullptr;
	SpellEntry const* pRockbiterWeapon = nullptr;
	SpellEntry const* pWindfuryWeapon = nullptr;
	SpellEntry const* pFlametongueWeapon = nullptr;

	// Mage Polymorph
	SpellEntry const* pPolymorphSheep = nullptr;
	SpellEntry const* pPolymorphCow = nullptr;
	SpellEntry const* pPolymorphPig = nullptr;
	SpellEntry const* pPolymorphTurtle = nullptr;

	// Mage Frost Armor (to replace ice armor at low level)
	SpellEntry const* pFrostArmor = nullptr;

	//bool hasDeadlyPoison = false;
	//bool hasCripplingPoison = false;
	//bool hasWoundPoison = false;
	//bool HasMindNumbingPoison = false;

	for (const auto& [spellID, spell] : me->GetSpellMap()) // me->GetSpellMap() returns the Bot's known spells
	{
		if (spell.disabled)
			continue;

		if (spell.state == PLAYERSPELL_REMOVED)
			continue;

		auto const* pSpellEntry = sSpellTemplate.LookupEntry<SpellEntry>(spellID);
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

		switch (me->getClass())
		{
		case CLASS_WARRIOR:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Battle Shout") == 0) // SpellName[0] -> 0 means the index of Localisation langue in this case enUS
			{
				if (!m_spells.warrior.pBattleShout ||
					m_spells.warrior.pBattleShout->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pBattleShout = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Devastate") == 0)
			{
				if (!m_spells.warrior.pDevastate ||
					m_spells.warrior.pDevastate->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pDevastate = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Battle Stance") == 0)
			{
				if (!m_spells.warrior.pBattleStance ||
					m_spells.warrior.pBattleStance->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pBattleStance = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Heroic Strike") == 0)
			{
				if (!m_spells.warrior.pHeroicStrike ||
					m_spells.warrior.pHeroicStrike->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pHeroicStrike = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Charge") == 0)
			{
				if (!m_spells.warrior.pCharge ||
					m_spells.warrior.pCharge->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pCharge = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Rend") == 0)
			{
				if (!m_spells.warrior.pRend ||
					m_spells.warrior.pRend->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pRend = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Thunder Clap") == 0)
			{
				if (!m_spells.warrior.pThunderClap ||
					m_spells.warrior.pThunderClap->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pThunderClap = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Hamstring") == 0)
			{
				if (!m_spells.warrior.pHamstring ||
					m_spells.warrior.pHamstring->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pHamstring = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Defensive Stance") == 0)
			{
				if (!m_spells.warrior.pDefensiveStance ||
					m_spells.warrior.pDefensiveStance->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pDefensiveStance = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Bloodrage") == 0)
			{
				if (!m_spells.warrior.pBloodrage ||
					m_spells.warrior.pBloodrage->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pBloodrage = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Taunt") == 0)
			{
				if (!m_spells.warrior.pTaunt ||
					m_spells.warrior.pTaunt->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pTaunt = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Sunder Armor") == 0)
			{
				if (!m_spells.warrior.pSunderArmor ||
					m_spells.warrior.pSunderArmor->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pSunderArmor = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Overpower") == 0)
			{
				if (!m_spells.warrior.pOverpower ||
					m_spells.warrior.pOverpower->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pOverpower = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shield Bash") == 0)
			{
				if (!m_spells.warrior.pShieldBash ||
					m_spells.warrior.pShieldBash->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pShieldBash = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Revenge") == 0)
			{
				if (!m_spells.warrior.pRevenge ||
					m_spells.warrior.pRevenge->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pRevenge = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Demoralizing Shout") == 0)
			{
				if (!m_spells.warrior.pDemoralizingShout ||
					m_spells.warrior.pDemoralizingShout->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pDemoralizingShout = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shield Block") == 0)
			{
				if (!m_spells.warrior.pShieldBlock ||
					m_spells.warrior.pShieldBlock->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pShieldBlock = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mocking Blow") == 0)
			{
				if (!m_spells.warrior.pMockingBlow ||
					m_spells.warrior.pMockingBlow->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pMockingBlow = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Disarm") == 0)
			{
				if (!m_spells.warrior.pDisarm ||
					m_spells.warrior.pDisarm->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pDisarm = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Cleave") == 0)
			{
				if (!m_spells.warrior.pCleave ||
					m_spells.warrior.pCleave->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pCleave = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Retaliation") == 0)
			{
				if (!m_spells.warrior.pRetaliation ||
					m_spells.warrior.pRetaliation->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pRetaliation = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Intimidating Shout") == 0)
			{
				if (!m_spells.warrior.pIntimidatingShout ||
					m_spells.warrior.pIntimidatingShout->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pIntimidatingShout = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Execute") == 0)
			{
				if (!m_spells.warrior.pExecute ||
					m_spells.warrior.pExecute->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pExecute = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Challenging Shout") == 0)
			{
				if (!m_spells.warrior.pChallengingShout ||
					m_spells.warrior.pChallengingShout->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pChallengingShout = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shield Wall") == 0)
			{
				if (!m_spells.warrior.pShieldWall ||
					m_spells.warrior.pShieldWall->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pShieldWall = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Berserker Stance") == 0)
			{
				if (!m_spells.warrior.pBerserkerStance ||
					m_spells.warrior.pBerserkerStance->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pBerserkerStance = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Intercept") == 0)
			{
				if (!m_spells.warrior.pIntercept ||
					m_spells.warrior.pIntercept->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pIntercept = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Slam") == 0)
			{
				if (!m_spells.warrior.pSlam ||
					m_spells.warrior.pSlam->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pSlam = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Berserker Rage") == 0)
			{
				if (!m_spells.warrior.pBerserkerRage ||
					m_spells.warrior.pBerserkerRage->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pBerserkerRage = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Whirlwind") == 0)
			{
				if (!m_spells.warrior.pWhirlwind ||
					m_spells.warrior.pWhirlwind->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pWhirlwind = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Pummel") == 0)
			{
				if (!m_spells.warrior.pPummel ||
					m_spells.warrior.pPummel->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pPummel = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Recklessness") == 0)
			{
				if (!m_spells.warrior.pRecklessness ||
					m_spells.warrior.pRecklessness->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pRecklessness = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Victory Rush") == 0)
			{
				if (!m_spells.warrior.pVictoryRush ||
					m_spells.warrior.pVictoryRush->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pVictoryRush = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Spell Reflection") == 0)
			{
				if (!m_spells.warrior.pSpellReflection ||
					m_spells.warrior.pSpellReflection->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pSpellReflection = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Commanding Shout") == 0)
			{
				if (!m_spells.warrior.pCommandingShout ||
					m_spells.warrior.pCommandingShout->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pCommandingShout = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Intervene") == 0)
			{
				if (!m_spells.warrior.pIntervene ||
					m_spells.warrior.pIntervene->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pIntervene = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Death Wish") == 0)
			{
				if (!m_spells.warrior.pDeathWish ||
					m_spells.warrior.pDeathWish->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pDeathWish = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mortal Strike") == 0)
			{
				if (!m_spells.warrior.pMortalStrike ||
					m_spells.warrior.pMortalStrike->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pMortalStrike = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Piercing Howl") == 0)
			{
				if (!m_spells.warrior.pPiercingHowl ||
					m_spells.warrior.pPiercingHowl->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pPiercingHowl = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Sweeping Strikes") == 0)
			{
				if (!m_spells.warrior.pSweepingStrikes ||
					m_spells.warrior.pSweepingStrikes->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pSweepingStrikes = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Bloodthirst") == 0)
			{
				if (!m_spells.warrior.pBloodthirst ||
					m_spells.warrior.pBloodthirst->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pBloodthirst = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Rampage") == 0)
			{
				if (!m_spells.warrior.pRampage ||
					m_spells.warrior.pRampage->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pRampage = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Last Stand") == 0)
			{
				if (!m_spells.warrior.pLastStand ||
					m_spells.warrior.pLastStand->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pLastStand = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Concussion Blow") == 0)
			{
				if (!m_spells.warrior.pConcussionBlow ||
					m_spells.warrior.pConcussionBlow->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pConcussionBlow = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shield Slam") == 0)
			{
				if (!m_spells.warrior.pShieldSlam ||
					m_spells.warrior.pShieldSlam->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warrior.pShieldSlam = pSpellEntry;
			}
			break;
		}
		case CLASS_HUNTER:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Aspect of the Beast") == 0)
			{
				if (!m_spells.hunter.pAspectOfTheBeast ||
					m_spells.hunter.pAspectOfTheBeast->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pAspectOfTheBeast = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Aspect of the Hawk") == 0)
			{
				if (!m_spells.hunter.pAspectOfTheHawk ||
					m_spells.hunter.pAspectOfTheHawk->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pAspectOfTheHawk = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Aspect of the Cheetah") == 0)
			{
				if (!m_spells.hunter.pAspectOfTheCheetah ||
					m_spells.hunter.pAspectOfTheCheetah->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pAspectOfTheCheetah = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Aspect of the Monkey") == 0)
			{
				if (!m_spells.hunter.pAspectOfTheMonkey ||
					m_spells.hunter.pAspectOfTheMonkey->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pAspectOfTheMonkey = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Aspect of the Pack") == 0)
			{
				if (!m_spells.hunter.pAspectOfThePack ||
					m_spells.hunter.pAspectOfThePack->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pAspectOfThePack = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Aspect of the Viper") == 0)
			{
				if (!m_spells.hunter.pAspectOfTheViper ||
					m_spells.hunter.pAspectOfTheViper->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pAspectOfTheViper = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Aspect of the Wild") == 0)
			{
				if (!m_spells.hunter.pAspectOfTheWild ||
					m_spells.hunter.pAspectOfTheWild->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pAspectOfTheWild = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Eagle Eye") == 0)
			{
				if (!m_spells.hunter.pEagleEye ||
					m_spells.hunter.pEagleEye->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pEagleEye = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Eyes of the Beast") == 0)
			{
				if (!m_spells.hunter.pEyeOfTheBeast ||
					m_spells.hunter.pEyeOfTheBeast->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pEyeOfTheBeast = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mend Pet") == 0)
			{
				if (!m_spells.hunter.pMendPet ||
					m_spells.hunter.pMendPet->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pMendPet = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Intimidation") == 0)
			{
				if (!m_spells.hunter.pIntimidation ||
					m_spells.hunter.pIntimidation->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pIntimidation = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Beast Lore") == 0)
			{
				if (!m_spells.hunter.pBeastLore ||
					m_spells.hunter.pBeastLore->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pBeastLore = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Kill Command") == 0)
			{
				if (!m_spells.hunter.pKillCommand ||
					m_spells.hunter.pKillCommand->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pKillCommand = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Bestial Wrath") == 0)
			{
				if (!m_spells.hunter.pBestialWrath ||
					m_spells.hunter.pBestialWrath->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pBestialWrath = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Revive Pet") == 0)
			{
				if (!m_spells.hunter.pRevivePet ||
					m_spells.hunter.pRevivePet->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pRevivePet = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Scare Beast") == 0)
			{
				if (!m_spells.hunter.pScareBeast ||
					m_spells.hunter.pScareBeast->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pScareBeast = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Aimed Shot") == 0)
			{
				if (!m_spells.hunter.pAimedShot ||
					m_spells.hunter.pAimedShot->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pAimedShot = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Arcane Shot") == 0)
			{
				if (!m_spells.hunter.pArcaneShot ||
					m_spells.hunter.pArcaneShot->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pArcaneShot = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Distracting Shot") == 0)
			{
				if (!m_spells.hunter.pDistractingShot ||
					m_spells.hunter.pDistractingShot->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pDistractingShot = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Concussive Shot") == 0)
			{
				if (!m_spells.hunter.pConcussiveShot ||
					m_spells.hunter.pConcussiveShot->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pConcussiveShot = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Flare") == 0)
			{
				if (!m_spells.hunter.pFlare ||
					m_spells.hunter.pFlare->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pFlare = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Multi-Shot") == 0)
			{
				if (!m_spells.hunter.pMultiShot ||
					m_spells.hunter.pMultiShot->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pMultiShot = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Hunter's Mark") == 0)
			{
				if (!m_spells.hunter.pHuntersMark ||
					m_spells.hunter.pHuntersMark->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pHuntersMark = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Rapid Fire") == 0)
			{
				if (!m_spells.hunter.pRapidFire ||
					m_spells.hunter.pRapidFire->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pRapidFire = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Serpent Sting") == 0)
			{
				if (!m_spells.hunter.pSerpentSting ||
					m_spells.hunter.pSerpentSting->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pSerpentSting = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Scorpid Sting") == 0)
			{
				if (!m_spells.hunter.pScorpidSting ||
					m_spells.hunter.pScorpidSting->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pScorpidSting = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Scatter Shot") == 0)
			{
				if (!m_spells.hunter.pScatterShot ||
					m_spells.hunter.pScatterShot->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pScatterShot = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Viper Sting") == 0)
			{
				if (!m_spells.hunter.pViperSting ||
					m_spells.hunter.pViperSting->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pViperSting = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Silencing Shot") == 0)
			{
				if (!m_spells.hunter.pSilencingShot ||
					m_spells.hunter.pSilencingShot->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pSilencingShot = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Steady Shot") == 0)
			{
				if (!m_spells.hunter.pSteadyShot ||
					m_spells.hunter.pSteadyShot->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pSteadyShot = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Tranquilizing Shot") == 0)
			{
				if (!m_spells.hunter.pTranquilizingShot ||
					m_spells.hunter.pTranquilizingShot->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pTranquilizingShot = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Volley") == 0)
			{
				if (!m_spells.hunter.pVolley ||
					m_spells.hunter.pVolley->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pVolley = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Trueshot Aura") == 0)
			{
				if (!m_spells.hunter.pTrueshotAura ||
					m_spells.hunter.pTrueshotAura->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pTrueshotAura = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Counterattack") == 0)
			{
				if (!m_spells.hunter.pCounterattack ||
					m_spells.hunter.pCounterattack->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pCounterattack = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Disengage") == 0)
			{
				if (!m_spells.hunter.pDisengage ||
					m_spells.hunter.pDisengage->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pDisengage = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Explosive Trap") == 0)
			{
				if (!m_spells.hunter.pExplosiveTrap ||
					m_spells.hunter.pExplosiveTrap->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pExplosiveTrap = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Deterrence") == 0)
			{
				if (!m_spells.hunter.pDeterrence ||
					m_spells.hunter.pDeterrence->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pDeterrence = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Freezing Trap") == 0)
			{
				if (!m_spells.hunter.pFreezingTrap ||
					m_spells.hunter.pFreezingTrap->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pFreezingTrap = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Immolation Trap") == 0)
			{
				if (!m_spells.hunter.pImmolationTrap ||
					m_spells.hunter.pImmolationTrap->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pImmolationTrap = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Feign Death") == 0)
			{
				if (!m_spells.hunter.pFeignDeath ||
					m_spells.hunter.pFeignDeath->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pFeignDeath = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Misdirection") == 0)
			{
				if (!m_spells.hunter.pMisdirection ||
					m_spells.hunter.pMisdirection->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pMisdirection = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mongoose Bite") == 0)
			{
				if (!m_spells.hunter.pMongooseBite ||
					m_spells.hunter.pMongooseBite->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pMongooseBite = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Raptor Strike") == 0)
			{
				if (!m_spells.hunter.pRaptorStrike ||
					m_spells.hunter.pRaptorStrike->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pRaptorStrike = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Readiness") == 0)
			{
				if (!m_spells.hunter.pReadiness ||
					m_spells.hunter.pReadiness->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pReadiness = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Sname Trap") == 0)
			{
				if (!m_spells.hunter.pSnakeTrap ||
					m_spells.hunter.pSnakeTrap->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pSnakeTrap = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Track Giants") == 0)
			{
				if (!m_spells.hunter.pTrackGiants ||
					m_spells.hunter.pTrackGiants->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pTrackGiants = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Track Hidden") == 0)
			{
				if (!m_spells.hunter.pTrackHiden ||
					m_spells.hunter.pTrackHiden->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pTrackHiden = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Track Beasts") == 0)
			{
				if (!m_spells.hunter.pTrackBeasts ||
					m_spells.hunter.pTrackBeasts->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pTrackBeasts = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Track Humanoids") == 0)
			{
				if (!m_spells.hunter.pTrackHumanoids ||
					m_spells.hunter.pTrackHumanoids->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pTrackHumanoids = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Track Demons") == 0)
			{
				if (!m_spells.hunter.pTrackDemons ||
					m_spells.hunter.pTrackDemons->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pTrackDemons = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Track Undead") == 0)
			{
				if (!m_spells.hunter.pTrackUndead ||
					m_spells.hunter.pTrackUndead->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pTrackUndead = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Track Dragonkin") == 0)
			{
				if (!m_spells.hunter.pTrackDragonkin ||
					m_spells.hunter.pTrackDragonkin->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pTrackDragonkin = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Track Elementals") == 0)
			{
				if (!m_spells.hunter.pTrackElementals ||
					m_spells.hunter.pTrackElementals->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pTrackElementals = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Wing Clip") == 0)
			{
				if (!m_spells.hunter.pWingClip ||
					m_spells.hunter.pWingClip->Rank[0] < pSpellEntry->Rank[0])
					m_spells.hunter.pWingClip = pSpellEntry;
			}
			break;
		}
		case CLASS_DRUID:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Barkskin") == 0)
			{
				if (!m_spells.druid.pBarkskin ||
					m_spells.druid.pBarkskin->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pBarkskin = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Entangling Roots") == 0)
			{
				if (!m_spells.druid.pEntanglingRoots ||
					m_spells.druid.pEntanglingRoots->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pEntanglingRoots = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Faerie Fire") == 0)
			{
				if (!m_spells.druid.pFaerieFire ||
					m_spells.druid.pFaerieFire->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pFaerieFire = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Faerie Fire (Feral)") == 0)
			{
				if (!m_spells.druid.pFaerieFireFeral ||
					m_spells.druid.pFaerieFireFeral->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pFaerieFireFeral = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Force of Nature") == 0)
			{
				if (!m_spells.druid.pForceOfNature ||
					m_spells.druid.pForceOfNature->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pForceOfNature = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Hibernate") == 0)
			{
				if (!m_spells.druid.pHibernate ||
					m_spells.druid.pHibernate->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pHibernate = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Hurricane") == 0)
			{
				if (!m_spells.druid.pHurricane ||
					m_spells.druid.pHurricane->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pHurricane = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Innervate") == 0)
			{
				if (!m_spells.druid.pInnervate ||
					m_spells.druid.pInnervate->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pInnervate = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Insect Swarm") == 0)
			{
				if (!m_spells.druid.pInsectSwarm ||
					m_spells.druid.pInsectSwarm->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pInsectSwarm = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Moonfire") == 0)
			{
				if (!m_spells.druid.pMoonfire ||
					m_spells.druid.pMoonfire->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pMoonfire = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Moonkin Form") == 0)
			{
				if (!m_spells.druid.pMoonkinForm ||
					m_spells.druid.pMoonkinForm->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pMoonkinForm = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Nature's Grasp") == 0)
			{
				if (!m_spells.druid.pNaturesGrasp ||
					m_spells.druid.pNaturesGrasp->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pNaturesGrasp = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Smoothe Animal") == 0)
			{
				if (!m_spells.druid.pSootheAnimal ||
					m_spells.druid.pSootheAnimal->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pSootheAnimal = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Starfire") == 0)
			{
				if (!m_spells.druid.pStarfire ||
					m_spells.druid.pStarfire->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pStarfire = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Teleport: Moonglade") == 0)
			{
				if (!m_spells.druid.pTeleportMoonglade ||
					m_spells.druid.pTeleportMoonglade->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pTeleportMoonglade = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Thorns") == 0)
			{
				if (!m_spells.druid.pThorns ||
					m_spells.druid.pThorns->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pThorns = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Wrath") == 0)
			{
				if (!m_spells.druid.pWrath ||
					m_spells.druid.pWrath->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pWrath = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Aquatic Form") == 0)
			{
				if (!m_spells.druid.pAquaticForm ||
					m_spells.druid.pAquaticForm->Rank[0] < pSpellEntry->Rank[0])
					m_spells.druid.pAquaticForm = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Cower") == 0)
			{
			if (!m_spells.druid.pCower ||
				m_spells.druid.pCower->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pCower = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Dash") == 0)
			{
			if (!m_spells.druid.pDash ||
				m_spells.druid.pDash->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pDash = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Bash") == 0)
			{
			if (!m_spells.druid.pBash ||
				m_spells.druid.pBash->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pBash = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Demoralizing Roar") == 0)
			{
			if (!m_spells.druid.pDemoralisationRoar ||
				m_spells.druid.pDemoralisationRoar->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pDemoralisationRoar = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Cat Form") == 0)
			{
			if (!m_spells.druid.pCatForm ||
				m_spells.druid.pCatForm->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pCatForm = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Dire Bear Form") == 0)
			{
			if (!m_spells.druid.pDireBearForm ||
				m_spells.druid.pDireBearForm->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pDireBearForm = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Bear Form") == 0)
			{
			if (!m_spells.druid.pBearForm ||
				m_spells.druid.pBearForm->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pBearForm = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Challenging Roar") == 0)
			{
			if (!m_spells.druid.pChallengingRoar ||
				m_spells.druid.pChallengingRoar->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pChallengingRoar = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Enrage") == 0)
			{
			if (!m_spells.druid.pEnrage ||
				m_spells.druid.pEnrage->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pEnrage = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Claw") == 0)
			{
			if (!m_spells.druid.pClaw ||
				m_spells.druid.pClaw->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pClaw = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ferocious Bite") == 0)
			{
			if (!m_spells.druid.pFerociousBite ||
				m_spells.druid.pFerociousBite->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pFerociousBite = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Pounce") == 0)
			{
			if (!m_spells.druid.pPounce ||
				m_spells.druid.pPounce->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pPounce = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Frenzied Regeneration") == 0)
			{
			if (!m_spells.druid.pFrenziedRegeneration ||
				m_spells.druid.pFrenziedRegeneration->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pFrenziedRegeneration = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Prowl") == 0)
			{
			if (!m_spells.druid.pProwl ||
				m_spells.druid.pProwl->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pProwl = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Growl") == 0)
			{
			if (!m_spells.druid.pGrowl ||
				m_spells.druid.pGrowl->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pGrowl = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Rake") == 0)
			{
			if (!m_spells.druid.pRake ||
				m_spells.druid.pRake->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pRake = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Lacerate") == 0)
			{
			if (!m_spells.druid.pLacerate ||
				m_spells.druid.pLacerate->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pLacerate = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ravage") == 0)
			{
			if (!m_spells.druid.pRavage ||
				m_spells.druid.pRavage->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pRavage = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Maim") == 0)
			{
			if (!m_spells.druid.pMaim ||
				m_spells.druid.pMaim->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pMaim = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Rip") == 0)
			{
			if (!m_spells.druid.pRip ||
				m_spells.druid.pRip->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pRip = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Maul") == 0)
			{
			if (!m_spells.druid.pMaul ||
				m_spells.druid.pMaul->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pMaul = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shred") == 0)
			{
			if (!m_spells.druid.pShred ||
				m_spells.druid.pShred->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pShred = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Swift Flight Form") == 0)
			{
			if (!m_spells.druid.pSwiftFlightForm ||
				m_spells.druid.pSwiftFlightForm->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pSwiftFlightForm = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Swipe") == 0)
			{
			if (!m_spells.druid.pSwipe ||
				m_spells.druid.pSwipe->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pSwipe = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Tiger's Fury") == 0)
			{
			if (!m_spells.druid.pTigersFury ||
				m_spells.druid.pTigersFury->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pTigersFury = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Track Humanoids") == 0)
			{
			if (!m_spells.druid.pTrackHumanoids ||
				m_spells.druid.pTrackHumanoids->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pTrackHumanoids = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Travel Form") == 0)
			{
			if (!m_spells.druid.pTravelForm ||
				m_spells.druid.pTravelForm->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pTravelForm = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Abolish Poison") == 0)
			{
			if (!m_spells.druid.pAbolishPoison ||
				m_spells.druid.pAbolishPoison->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pAbolishPoison = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Healing Touch") == 0)
			{
			if (!m_spells.druid.pHealingTouch ||
				m_spells.druid.pHealingTouch->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pHealingTouch = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Gift of the Wild") == 0)
			{
			if (!m_spells.druid.pGiftOfTheWild ||
				m_spells.druid.pGiftOfTheWild->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pGiftOfTheWild = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Lifebloom") == 0)
			{
			if (!m_spells.druid.pLifebloom ||
				m_spells.druid.pLifebloom->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pLifebloom = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mark of the Wild") == 0)
			{
			if (!m_spells.druid.pMarkOfTheWild ||
				m_spells.druid.pMarkOfTheWild->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pMarkOfTheWild = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Rebirth") == 0)
			{
			if (!m_spells.druid.pRebirth ||
				m_spells.druid.pRebirth->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pRebirth = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Regrowth") == 0)
			{
			if (!m_spells.druid.pRegrowth ||
				m_spells.druid.pRegrowth->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pRegrowth = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Rejuvenation") == 0)
			{
			if (!m_spells.druid.pRejuvenation ||
				m_spells.druid.pRejuvenation->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pRejuvenation = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Remove Curse") == 0)
			{
			if (!m_spells.druid.pRemoveCurse ||
				m_spells.druid.pRemoveCurse->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pRemoveCurse = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Tranquility") == 0)
			{
			if (!m_spells.druid.pTranquility ||
				m_spells.druid.pTranquility->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pTranquility = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mangle (Bear)") == 0)
			{
			if (!m_spells.druid.pMangleBear ||
				m_spells.druid.pMangleBear->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pMangleBear = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mangle (Cat)") == 0)
			{
			if (!m_spells.druid.pMangleCat ||
				m_spells.druid.pMangleCat->Rank[0] < pSpellEntry->Rank[0])
				m_spells.druid.pMangleCat = pSpellEntry;
			}
			break;
		}
		case CLASS_MAGE:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "BlastWave") == 0)
			{
				if (!m_spells.mage.pBlastWave ||
					m_spells.mage.pBlastWave->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pBlastWave = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blazing Speed") == 0)
			{
				if (!m_spells.mage.pBlazingSpeed ||
					m_spells.mage.pBlazingSpeed->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pBlazingSpeed = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Combustion") == 0)
			{
				if (!m_spells.mage.pCombustion ||
					m_spells.mage.pCombustion->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pCombustion = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Dragon's Breath") == 0)
			{
				if (!m_spells.mage.pDragonsBreath ||
					m_spells.mage.pDragonsBreath->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pDragonsBreath = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Fire Blast") == 0)
			{
				if (!m_spells.mage.pFireBlast ||
					m_spells.mage.pFireBlast->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pFireBlast = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Fire Ward") == 0)
			{
				if (!m_spells.mage.pFireWard ||
					m_spells.mage.pFireWard->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pFireWard = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Fireball") == 0)
			{
				if (!m_spells.mage.pFireball ||
					m_spells.mage.pFireball->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pFireball = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Flamestrike") == 0)
			{
				if (!m_spells.mage.pFlamestrike ||
					m_spells.mage.pFlamestrike->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pFlamestrike = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Pyroblast") == 0)
			{
				if (!m_spells.mage.pPyroblast ||
					m_spells.mage.pPyroblast->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pPyroblast = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Scorch") == 0)
			{
				if (!m_spells.mage.pScorch ||
					m_spells.mage.pScorch->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pScorch = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Amplify Magic") == 0)
			{
				if (!m_spells.mage.pAmplityMagic ||
					m_spells.mage.pAmplityMagic->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pAmplityMagic = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Arcane Blast") == 0)
			{
				if (!m_spells.mage.pArcaneBlast ||
					m_spells.mage.pArcaneBlast->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pArcaneBlast = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Arcane Explosion") == 0)
			{
				if (!m_spells.mage.pArcaneExplosion ||
					m_spells.mage.pArcaneExplosion->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pArcaneExplosion = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Arcane Intellect") == 0)
			{
				if (!m_spells.mage.pArcaneIntellect ||
					m_spells.mage.pArcaneIntellect->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pArcaneIntellect = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Arcane Missile") == 0)
			{
				if (!m_spells.mage.pArcaneMissiles ||
					m_spells.mage.pArcaneMissiles->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pArcaneMissiles = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Arcane Power") == 0)
			{
				if (!m_spells.mage.pArcanePower ||
					m_spells.mage.pArcanePower->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pArcanePower = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blink") == 0)
			{
				if (!m_spells.mage.pBlink ||
					m_spells.mage.pBlink->Rank[0] < pSpellEntry->Rank[0])
					m_spells.mage.pBlink = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Confure Food") == 0)
			{
			if (!m_spells.mage.pConjureFood ||
				m_spells.mage.pConjureFood->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pConjureFood = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Confure Water") == 0)
			{
			if (!m_spells.mage.pConjureWater ||
				m_spells.mage.pConjureWater->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pConjureWater = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Counterspell") == 0)
			{
			if (!m_spells.mage.pCounterspell ||
				m_spells.mage.pCounterspell->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pCounterspell = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Conjure Mana Agate") == 0)
			{
			if (!m_spells.mage.pConjureManaAgate ||
				m_spells.mage.pConjureManaAgate->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pConjureManaAgate = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Conjure Mana Citrine") == 0)
			{
			if (!m_spells.mage.pConjureManaCitrine ||
				m_spells.mage.pConjureManaCitrine->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pConjureManaCitrine = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Conjure Mana Emerald") == 0)
			{
			if (!m_spells.mage.pConjureManaEmerald ||
				m_spells.mage.pConjureManaEmerald->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pConjureManaEmerald = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Conjure Mana Jade") == 0)
			{
			if (!m_spells.mage.pConjureManaJade ||
				m_spells.mage.pConjureManaJade->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pConjureManaJade = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Conjure Mana Ruby") == 0)
			{
			if (!m_spells.mage.pConjureManaRuby ||
				m_spells.mage.pConjureManaRuby->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pConjureManaRuby = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Dampen Magic") == 0)
			{
			if (!m_spells.mage.pDampenMagic ||
				m_spells.mage.pDampenMagic->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pDampenMagic = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Evocation") == 0)
			{
			if (!m_spells.mage.pEvocation ||
				m_spells.mage.pEvocation->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pEvocation = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Invisibility") == 0)
			{
			if (!m_spells.mage.pInvisibility ||
				m_spells.mage.pInvisibility->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pInvisibility = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mage Armor") == 0)
			{
			if (!m_spells.mage.pMageArmor ||
				m_spells.mage.pMageArmor->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pMageArmor = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mana Shield") == 0)
			{
			if (!m_spells.mage.pManaShield ||
				m_spells.mage.pManaShield->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pManaShield = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Polymorph") == 0)
			{
			if (!m_spells.mage.pPolymorph ||
				m_spells.mage.pPolymorph->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPolymorph = pSpellEntry;
				pPolymorphSheep = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Polymorph: Cow") == 0)
			{
			if (!pPolymorphCow ||
				pPolymorphCow->Rank[0] < pSpellEntry->Rank[0])
				pPolymorphCow = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Polymorph: Pig") == 0)
			{
			if (!pPolymorphPig ||
				pPolymorphPig->Rank[0] < pSpellEntry->Rank[0])
				pPolymorphPig = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Polymorph: Turtle") == 0)
			{
			if (!pPolymorphTurtle ||
				pPolymorphTurtle->Rank[0] < pSpellEntry->Rank[0])
				pPolymorphTurtle = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Presence of Mind") == 0)
			{
			if (!m_spells.mage.pPresenceOfMind||
				m_spells.mage.pPresenceOfMind->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPresenceOfMind = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Remove Lesser Curse") == 0)
			{
			if (!m_spells.mage.pRemoveLesserCurse ||
				m_spells.mage.pRemoveLesserCurse->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pRemoveLesserCurse = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ritual of Refreshment") == 0)
			{
			if (!m_spells.mage.pRitualOfRefreshment ||
				m_spells.mage.pRitualOfRefreshment->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pRitualOfRefreshment = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Slow") == 0)
			{
			if (!m_spells.mage.pSlow ||
				m_spells.mage.pSlow->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pSlow = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Slow Fall") == 0)
			{
			if (!m_spells.mage.pSlowFall ||
				m_spells.mage.pSlowFall->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pSlowFall = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Spellsteal") == 0)
			{
			if (!m_spells.mage.pSpellsteal ||
				m_spells.mage.pSpellsteal->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pSpellsteal = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Portal: Orgrimmar") == 0)
			{
			if (!m_spells.mage.pPortalToOrgrimmar ||
				m_spells.mage.pPortalToOrgrimmar->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPortalToOrgrimmar = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Portal: Shattrath") == 0)
			{
			if (!m_spells.mage.pPortalToShattrath ||
				m_spells.mage.pPortalToShattrath->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPortalToShattrath = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Portal: Silvermoon") == 0)
			{
			if (!m_spells.mage.pPortalToSilvermoon ||
				m_spells.mage.pPortalToSilvermoon->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPortalToSilvermoon = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Teleport: Undercity") == 0)
			{
			if (!m_spells.mage.pTeleportToUndercity ||
				m_spells.mage.pTeleportToUndercity->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pTeleportToUndercity = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Teleport: Silvermoon") == 0)
			{
			if (!m_spells.mage.pTeleportToSilvermoon ||
				m_spells.mage.pTeleportToSilvermoon->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pTeleportToSilvermoon = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Teleport: Shattrath") == 0)
			{
			if (!m_spells.mage.pTeleportToShattrath ||
				m_spells.mage.pTeleportToShattrath->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pTeleportToShattrath = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Teleport: Orgrimmar") == 0)
			{
			if (!m_spells.mage.pTeleportToOrgrimmar ||
				m_spells.mage.pTeleportToOrgrimmar->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pTeleportToOrgrimmar = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Teleport: Darnassus") == 0)
			{
			if (!m_spells.mage.pTeleportToDarnassus ||
				m_spells.mage.pTeleportToDarnassus->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pTeleportToDarnassus = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Portal: Darnassus") == 0)
			{
			if (!m_spells.mage.pPortalToDarnassus ||
				m_spells.mage.pPortalToDarnassus->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPortalToDarnassus = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Portal: Exodar") == 0)
			{
			if (!m_spells.mage.pPortalToExodar ||
				m_spells.mage.pPortalToExodar->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPortalToExodar = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Teleport: Exodar") == 0)
			{
			if (!m_spells.mage.pTeleportToExodar ||
				m_spells.mage.pTeleportToExodar->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pTeleportToExodar = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Teleport: Teramore") == 0)
			{
			if (!m_spells.mage.pTeleportToTeramore ||
				m_spells.mage.pTeleportToTeramore->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pTeleportToTeramore = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Portal: Teramore") == 0)
			{
			if (!m_spells.mage.pPortalToTeramore ||
				m_spells.mage.pPortalToTeramore->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPortalToTeramore = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Portal: IronForge") == 0)
			{
			if (!m_spells.mage.pPortalToIronForge ||
				m_spells.mage.pPortalToIronForge->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPortalToIronForge = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Teleport: Iron Forge") == 0)
			{
			if (!m_spells.mage.pTeleportToIronForge ||
				m_spells.mage.pTeleportToIronForge->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pTeleportToIronForge = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Teleport: Stormwind") == 0)
			{
			if (!m_spells.mage.pTeleportToStormwind ||
				m_spells.mage.pTeleportToStormwind->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pTeleportToStormwind = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Portal: Stormwind") == 0)
			{
			if (!m_spells.mage.pPortalToStormwind ||
				m_spells.mage.pPortalToStormwind->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPortalToStormwind = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Teleport: Thunder Bluff") == 0)
			{
			if (!m_spells.mage.pTeleportToThunderBluff ||
				m_spells.mage.pTeleportToThunderBluff->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pTeleportToThunderBluff = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Portal: Thunder Bluff") == 0)
			{
			if (!m_spells.mage.pPortalToThunderBluff ||
				m_spells.mage.pPortalToThunderBluff->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPortalToThunderBluff = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Teleport: Stonard") == 0)
			{
			if (!m_spells.mage.pTeleportToStonard ||
				m_spells.mage.pTeleportToStonard->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pTeleportToStonard = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Portal: Stonard") == 0)
			{
			if (!m_spells.mage.pPortalToStonard ||
				m_spells.mage.pPortalToStonard->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPortalToStonard = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Portal: Undercity") == 0)
			{
			if (!m_spells.mage.pPortalToUndercity ||
				m_spells.mage.pPortalToUndercity->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pPortalToUndercity = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blizzard") == 0)
			{
			if (!m_spells.mage.pBlizzard ||
				m_spells.mage.pBlizzard->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pBlizzard = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Cone of Cold") == 0)
			{
			if (!m_spells.mage.pConeOfCold ||
				m_spells.mage.pConeOfCold->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pConeOfCold = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Cold Snap") == 0)
			{
			if (!m_spells.mage.pColdSnap ||
				m_spells.mage.pColdSnap->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pColdSnap = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Frost Armor") == 0)
			{
			if (!m_spells.mage.pFrostArmor ||
				m_spells.mage.pFrostArmor->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pFrostArmor = pSpellEntry;
				pFrostArmor = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Frost Nova") == 0)
			{
			if (!m_spells.mage.pFrostNova ||
				m_spells.mage.pFrostNova->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pFrostNova = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Frost Ward") == 0)
			{
			if (!m_spells.mage.pFrostWard ||
				m_spells.mage.pFrostWard->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pFrostWard = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Frostbolt") == 0)
			{
			if (!m_spells.mage.pFrostbolt ||
				m_spells.mage.pFrostbolt->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pFrostbolt = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ice Armor") == 0)
			{
			if (!m_spells.mage.pIceArmor ||
				m_spells.mage.pIceArmor->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pIceArmor = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ice Barier") == 0)
			{
			if (!m_spells.mage.pIceBarier ||
				m_spells.mage.pIceBarier->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pIceBarier = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ice Block") == 0)
			{
			if (!m_spells.mage.pIceBlock ||
				m_spells.mage.pIceBlock->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pIceBlock = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ice Lance") == 0)
			{
			if (!m_spells.mage.pIceLance ||
				m_spells.mage.pIceLance->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pIceLance = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Icy Veind") == 0)
			{
			if (!m_spells.mage.pIcyVeins ||
				m_spells.mage.pIcyVeins->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pIcyVeins = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Summon Water Elemental") == 0)
			{
			if (!m_spells.mage.pSummonWaterElemental ||
				m_spells.mage.pSummonWaterElemental->Rank[0] < pSpellEntry->Rank[0])
				m_spells.mage.pSummonWaterElemental = pSpellEntry;
			}
			break;
		}
		case CLASS_PALADIN:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Blessing of Light") == 0)
			{
				if (!m_spells.paladin.pBlessingOfLight ||
					m_spells.paladin.pBlessingOfLight->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pBlessingOfLight = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blessing of Wisdom") == 0)
			{
				if (!m_spells.paladin.pBlessingOfWisdom ||
					m_spells.paladin.pBlessingOfWisdom->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pBlessingOfWisdom = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Cleanse") == 0)
			{
				if (!m_spells.paladin.pCleanse ||
					m_spells.paladin.pCleanse->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pCleanse = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Consecration") == 0)
			{
				if (!m_spells.paladin.pConsecration ||
					m_spells.paladin.pConsecration->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pConsecration = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Divine Illumination") == 0)
			{
				if (!m_spells.paladin.pDivineIllumination ||
					m_spells.paladin.pDivineIllumination->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pDivineIllumination = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Exorcism") == 0)
			{
				if (!m_spells.paladin.pExorcism ||
					m_spells.paladin.pExorcism->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pExorcism = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Flash of Light") == 0)
			{
				if (!m_spells.paladin.pFlashOfLight ||
					m_spells.paladin.pFlashOfLight->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pFlashOfLight = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Blessing of Light") == 0)
			{
				if (!m_spells.paladin.pGreaterBlessingOfLight ||
					m_spells.paladin.pGreaterBlessingOfLight->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pGreaterBlessingOfLight = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Blessing of Wisdom") == 0)
			{
				if (!m_spells.paladin.pGreaterBlessingOfWisdom ||
					m_spells.paladin.pGreaterBlessingOfWisdom->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pGreaterBlessingOfWisdom = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Hanner of Wrath") == 0)
			{
				if (!m_spells.paladin.pHammerOfWrath ||
					m_spells.paladin.pHammerOfWrath->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pHammerOfWrath = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Holy Light") == 0)
			{
				if (!m_spells.paladin.pHolyLight ||
					m_spells.paladin.pHolyLight->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pHolyLight = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Holy Shock") == 0)
			{
				if (!m_spells.paladin.pHolyShock ||
					m_spells.paladin.pHolyShock->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pHolyShock = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Holy Wrath") == 0)
			{
				if (!m_spells.paladin.pHolyWrath ||
					m_spells.paladin.pHolyWrath->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pHolyWrath = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Lay on Hands") == 0)
			{
				if (!m_spells.paladin.pLayOnHands ||
					m_spells.paladin.pLayOnHands->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pLayOnHands = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Purify") == 0)
			{
				if (!m_spells.paladin.pPurify ||
					m_spells.paladin.pPurify->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pPurify = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Seal of Light") == 0)
			{
				if (!m_spells.paladin.pSealOfLight ||
					m_spells.paladin.pSealOfLight->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pSealOfLight = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Seal of Righhteousness") == 0)
			{
				if (!m_spells.paladin.pSealOfRighteousness ||
					m_spells.paladin.pSealOfRighteousness->Rank[0] < pSpellEntry->Rank[0])
					m_spells.paladin.pSealOfRighteousness = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Seal of Wisdom") == 0)
			{
			if (!m_spells.paladin.pSealOfWisdom ||
				m_spells.paladin.pSealOfWisdom->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pSealOfWisdom = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Summon Warhorse") == 0)
			{
			if (!m_spells.paladin.pSummonCharger ||
				m_spells.paladin.pSummonCharger->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pSummonCharger = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Turn Evil") == 0)
			{
			if (!m_spells.paladin.pTurnEvil ||
				m_spells.paladin.pTurnEvil->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pTurnEvil = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Turn Undead") == 0)
			{
			if (!m_spells.paladin.pTurnUndead ||
				m_spells.paladin.pTurnUndead->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pTurnUndead = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Avenger's Shield") == 0)
			{
			if (!m_spells.paladin.pAvengersShield ||
				m_spells.paladin.pAvengersShield->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pAvengersShield = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blessing of Freedom") == 0)
			{
			if (!m_spells.paladin.pBlessingOfFredom ||
				m_spells.paladin.pBlessingOfFredom->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pBlessingOfFredom = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blessing of Kings") == 0)
			{
			if (!m_spells.paladin.pBlessingOfKings ||
				m_spells.paladin.pBlessingOfKings->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pBlessingOfKings = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blessing of Protection") == 0)
			{
			if (!m_spells.paladin.pBlessingOfProtection ||
				m_spells.paladin.pBlessingOfProtection->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pBlessingOfProtection = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blessing of Sacrifice") == 0)
			{
			if (!m_spells.paladin.pBlessingOfSacrifice ||
				m_spells.paladin.pBlessingOfSacrifice->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pBlessingOfSacrifice = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blessing of Salvation") == 0)
			{
			if (!m_spells.paladin.pBlessingOfSalvation ||
				m_spells.paladin.pBlessingOfSalvation->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pBlessingOfSalvation = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blessing of Sanctuary") == 0)
			{
			if (!m_spells.paladin.pBlessingOfSanctuary ||
				m_spells.paladin.pBlessingOfSanctuary->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pBlessingOfSanctuary = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Concentration Aura") == 0)
			{
			if (!m_spells.paladin.pConcentrationAura ||
				m_spells.paladin.pConcentrationAura->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pConcentrationAura = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Devotion Aura") == 0)
			{
			if (!m_spells.paladin.pDevotionAura ||
				m_spells.paladin.pDevotionAura->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pDevotionAura = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Divine Intervention") == 0)
			{
			if (!m_spells.paladin.pDivineIntervention ||
				m_spells.paladin.pDivineIntervention->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pDivineIntervention = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Divine Protection") == 0)
			{
			if (!m_spells.paladin.pDivineProtection ||
				m_spells.paladin.pDivineProtection->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pDivineProtection = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Divine Shield") == 0)
			{
			if (!m_spells.paladin.pDivineShield ||
				m_spells.paladin.pDivineShield->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pDivineShield = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Fire Resistance Aura") == 0)
			{
			if (!m_spells.paladin.pFireResistanceAura ||
				m_spells.paladin.pFireResistanceAura->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pFireResistanceAura = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Blessing of Kings") == 0)
			{
			if (!m_spells.paladin.pGreaterBlessingOfKings ||
				m_spells.paladin.pGreaterBlessingOfKings->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pGreaterBlessingOfKings = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Blessing of Salvation") == 0)
			{
			if (!m_spells.paladin.pGreaterBlessingOfSalvation ||
				m_spells.paladin.pGreaterBlessingOfSalvation->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pGreaterBlessingOfSalvation = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Blessing of Sanctuary") == 0)
			{
			if (!m_spells.paladin.pGreaterBlessingOfSanctuary ||
				m_spells.paladin.pGreaterBlessingOfSanctuary->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pGreaterBlessingOfSanctuary = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Hammer of Justice") == 0)
			{
			if (!m_spells.paladin.pHammerOfJustice ||
				m_spells.paladin.pHammerOfJustice->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pHammerOfJustice = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Holy Shield") == 0)
			{
			if (!m_spells.paladin.pHolyShield ||
				m_spells.paladin.pHolyShield->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pHolyShield = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Righteous Defense") == 0)
			{
			if (!m_spells.paladin.pRighteousDefense ||
				m_spells.paladin.pRighteousDefense->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pRighteousDefense = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Righteous Fury") == 0)
			{
			if (!m_spells.paladin.pRighteousFury ||
				m_spells.paladin.pRighteousFury->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pRighteousFury = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Seal of Justice") == 0)
			{
			if (!m_spells.paladin.pSealOfJustice ||
				m_spells.paladin.pSealOfJustice->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pSealOfJustice = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shadow Resistance Aura") == 0)
			{
			if (!m_spells.paladin.pShadowResistanceAura ||
				m_spells.paladin.pShadowResistanceAura->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pShadowResistanceAura = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Avenging Wrath") == 0)
			{
			if (!m_spells.paladin.pAvengingWrath ||
				m_spells.paladin.pAvengingWrath->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pAvengingWrath = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blessing of Might") == 0)
			{
			if (!m_spells.paladin.pBlessingOfMight ||
				m_spells.paladin.pBlessingOfMight->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pBlessingOfMight = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Crusader Aura") == 0)
			{
			if (!m_spells.paladin.pCrusaderAura ||
				m_spells.paladin.pCrusaderAura->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pCrusaderAura = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Crusader Strike") == 0)
			{
			if (!m_spells.paladin.pCrusaderStrike ||
				m_spells.paladin.pCrusaderStrike->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pCrusaderStrike = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Blessing of Might") == 0)
			{
			if (!m_spells.paladin.pGreaterBlessingOfMight ||
				m_spells.paladin.pGreaterBlessingOfMight->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pGreaterBlessingOfMight = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Judgement") == 0)
			{
			if (!m_spells.paladin.pJudgement ||
				m_spells.paladin.pJudgement->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pJudgement = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Repentance") == 0)
			{
			if (!m_spells.paladin.pRepentance ||
				m_spells.paladin.pRepentance->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pRepentance = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Retribution Aura") == 0)
			{
			if (!m_spells.paladin.pRetributionAura ||
				m_spells.paladin.pRetributionAura->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pRetributionAura = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Sanctity Aura") == 0)
			{
			if (!m_spells.paladin.pSanctityAura ||
				m_spells.paladin.pSanctityAura->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pSanctityAura = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Seal of Command") == 0)
			{
			if (!m_spells.paladin.pSealOfCommand ||
				m_spells.paladin.pSealOfCommand->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pSealOfCommand = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Seal of the Crusader") == 0)
			{
			if (!m_spells.paladin.pSealOfTheCrusader ||
				m_spells.paladin.pSealOfTheCrusader->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pSealOfTheCrusader = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Seal of Vengeance") == 0)
			{
			if (!m_spells.paladin.pSealOfVengence ||
				m_spells.paladin.pSealOfVengence->Rank[0] < pSpellEntry->Rank[0])
				m_spells.paladin.pSealOfVengence = pSpellEntry;
			}
			break;
		}
		case CLASS_PRIEST:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Dispel Magic") == 0)
			{
				if (!m_spells.priest.pDispelMagic ||
					m_spells.priest.pDispelMagic->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pDispelMagic = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Divine Spirit") == 0)
			{
				if (!m_spells.priest.pDivineSpirit ||
					m_spells.priest.pDivineSpirit->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pDivineSpirit = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Fear Ward") == 0)
			{
				if (!m_spells.priest.pFearWard ||
					m_spells.priest.pFearWard->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pFearWard = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Inner Fire") == 0)
			{
				if (!m_spells.priest.pInnerFire ||
					m_spells.priest.pInnerFire->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pInnerFire = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Inner Focus") == 0)
			{
				if (!m_spells.priest.pInnerFocus ||
					m_spells.priest.pInnerFocus->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pInnerFocus = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Levitate") == 0)
			{
				if (!m_spells.priest.pLevitate ||
					m_spells.priest.pLevitate->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pLevitate = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mana Burn") == 0)
			{
				if (!m_spells.priest.pManaBurn ||
					m_spells.priest.pManaBurn->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pManaBurn = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mass Dispel") == 0)
			{
				if (!m_spells.priest.pMassDispel ||
					m_spells.priest.pMassDispel->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pMassDispel = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Pain Suppression") == 0)
			{
				if (!m_spells.priest.pPainSuppression ||
					m_spells.priest.pPainSuppression->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pPainSuppression = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Power Infusion") == 0)
			{
				if (!m_spells.priest.pPowerInfusion ||
					m_spells.priest.pPowerInfusion->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pPowerInfusion = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Power Word: Fortitude") == 0)
			{
				if (!m_spells.priest.pPowerWordFortitude ||
					m_spells.priest.pPowerWordFortitude->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pPowerWordFortitude = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Power Word: Shield") == 0)
			{
				if (!m_spells.priest.pPowerWordShield ||
					m_spells.priest.pPowerWordShield->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pPowerWordShield = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Fortitude") == 0)
			{
				if (!m_spells.priest.pPrayerOfFortitude ||
					m_spells.priest.pPrayerOfFortitude->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pPrayerOfFortitude = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Spirit") == 0)
			{
				if (!m_spells.priest.pPrayerOfSpirit ||
					m_spells.priest.pPrayerOfSpirit->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pPrayerOfSpirit = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shackle Undead") == 0)
			{
				if (!m_spells.priest.pShackleUndead ||
					m_spells.priest.pShackleUndead->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pShackleUndead = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Symbol of Hope") == 0)
			{
				if (!m_spells.priest.pSymbolOfHope ||
					m_spells.priest.pSymbolOfHope->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pSymbolOfHope = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Abolish Disease") == 0)
			{
				if (!m_spells.priest.pAbolishDisease ||
					m_spells.priest.pAbolishDisease->Rank[0] < pSpellEntry->Rank[0])
					m_spells.priest.pAbolishDisease = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Binding Heal") == 0)
			{
			if (!m_spells.priest.pBindingHeal ||
				m_spells.priest.pBindingHeal->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pBindingHeal = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Chastise") == 0)
			{
			if (!m_spells.priest.pChastise ||
				m_spells.priest.pChastise->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pChastise = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Circle of Healing") == 0)
			{
			if (!m_spells.priest.pCircleOfHealing ||
				m_spells.priest.pCircleOfHealing->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pCircleOfHealing = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Cure Disease") == 0)
			{
			if (!m_spells.priest.pCureDisease ||
				m_spells.priest.pCureDisease->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pCureDisease = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Flash Heal") == 0)
			{
			if (!m_spells.priest.pFlashHeal ||
				m_spells.priest.pFlashHeal->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pFlashHeal = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Heal") == 0)
			{
			if (!m_spells.priest.pGreaterHeal ||
				m_spells.priest.pGreaterHeal->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pGreaterHeal = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Heal") == 0)
			{
			if (!m_spells.priest.pHeal ||
				m_spells.priest.pHeal->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pHeal = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Holy Fire") == 0)
			{
			if (!m_spells.priest.pHolyFire ||
				m_spells.priest.pHolyFire->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pHolyFire = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Holy Nova") == 0)
			{
			if (!m_spells.priest.pHolyNova ||
				m_spells.priest.pHolyNova->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pHolyNova = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Lesser Heal") == 0)
			{
			if (!m_spells.priest.pLesserHeal ||
				m_spells.priest.pLesserHeal->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pLesserHeal = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Lightwell") == 0)
			{
			if (!m_spells.priest.pLightWell ||
				m_spells.priest.pLightWell->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pLightWell = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Healing") == 0)
			{
			if (!m_spells.priest.pPrayerHealing ||
				m_spells.priest.pPrayerHealing->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pPrayerHealing = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Mending") == 0)
			{
			if (!m_spells.priest.pPrayerOfMending ||
				m_spells.priest.pPrayerOfMending->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pPrayerOfMending = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Renew") == 0)
			{
			if (!m_spells.priest.pRenew ||
				m_spells.priest.pRenew->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pRenew = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Resurrection") == 0)
			{
			if (!m_spells.priest.pResurrection ||
				m_spells.priest.pResurrection->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pResurrection = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Smite") == 0)
			{
			if (!m_spells.priest.pSmite ||
				m_spells.priest.pSmite->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pSmite = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Fade") == 0)
			{
			if (!m_spells.priest.pFade ||
				m_spells.priest.pFade->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pFade = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mind Blast") == 0)
			{
			if (!m_spells.priest.pMindBlast ||
				m_spells.priest.pMindBlast->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pMindBlast = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mind Control") == 0)
			{
			if (!m_spells.priest.pMindControl ||
				m_spells.priest.pMindControl->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pMindControl = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mind Flay") == 0)
			{
			if (!m_spells.priest.pMindFlay ||
				m_spells.priest.pMindFlay->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pMindFlay = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mind Soothe") == 0)
			{
			if (!m_spells.priest.pMindSoothe ||
				m_spells.priest.pMindSoothe->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pMindSoothe = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mind Vision") == 0)
			{
			if (!m_spells.priest.pMindVision ||
				m_spells.priest.pMindVision->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pMindVision = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Shadow Protection") == 0)
			{
			if (!m_spells.priest.pPrayerOfShadowProtection ||
				m_spells.priest.pPrayerOfShadowProtection->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pPrayerOfShadowProtection = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Psychic Scream") == 0)
			{
			if (!m_spells.priest.pPsychicScream ||
				m_spells.priest.pPsychicScream->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pPsychicScream = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shadow Protection") == 0)
			{
			if (!m_spells.priest.pShadowProtection ||
				m_spells.priest.pShadowProtection->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pShadowProtection = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shadow Word: Death") == 0)
			{
			if (!m_spells.priest.pShadowWordDeath ||
				m_spells.priest.pShadowWordDeath->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pShadowWordDeath = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shadowfiend") == 0)
			{
			if (!m_spells.priest.pShadowfiend ||
				m_spells.priest.pShadowfiend->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pShadowfiend = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shadowform") == 0)
			{
			if (!m_spells.priest.pShadowform ||
				m_spells.priest.pShadowform->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pShadowform = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Silence") == 0)
			{
			if (!m_spells.priest.pSilence ||
				m_spells.priest.pSilence->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pSilence = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Vampiric Embrace") == 0)
			{
			if (!m_spells.priest.pVampiricEmbrace ||
				m_spells.priest.pVampiricEmbrace->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pVampiricEmbrace = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Vampiric Touch") == 0)
			{
			if (!m_spells.priest.pVampiricTouch ||
				m_spells.priest.pVampiricTouch->Rank[0] < pSpellEntry->Rank[0])
				m_spells.priest.pVampiricTouch = pSpellEntry;
			}
			break;
		}
		case CLASS_ROGUE:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Ambush") == 0)
			{
				if (!m_spells.rogue.pAmbush ||
					m_spells.rogue.pAmbush->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pAmbush = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Cheap Shot") == 0)
			{
				if (!m_spells.rogue.pCheapShot ||
					m_spells.rogue.pCheapShot->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pCheapShot = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Cold Blood") == 0)
			{
				if (!m_spells.rogue.pColdBlood ||
					m_spells.rogue.pColdBlood->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pColdBlood = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Deadly Throw") == 0)
			{
				if (!m_spells.rogue.pDeadlyThrow ||
					m_spells.rogue.pDeadlyThrow->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pDeadlyThrow = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Envenom") == 0)
			{
				if (!m_spells.rogue.pEnvenom ||
					m_spells.rogue.pEnvenom->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pEnvenom = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Eviscerate") == 0)
			{
				if (!m_spells.rogue.pEviscerate ||
					m_spells.rogue.pEviscerate->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pEviscerate = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Expose Armor") == 0)
			{
				if (!m_spells.rogue.pExposeArmor ||
					m_spells.rogue.pExposeArmor->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pExposeArmor = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Garrote") == 0)
			{
				if (!m_spells.rogue.pGarrote ||
					m_spells.rogue.pGarrote->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pGarrote = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Kidney Shot") == 0)
			{
				if (!m_spells.rogue.pKidneyShot ||
					m_spells.rogue.pKidneyShot->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pKidneyShot = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mutilate") == 0)
			{
				if (!m_spells.rogue.pMutilate ||
					m_spells.rogue.pMutilate->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pMutilate = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Rupture") == 0)
			{
				if (!m_spells.rogue.pRupture ||
					m_spells.rogue.pRupture->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pRupture = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Slice and Dice") == 0)
			{
				if (!m_spells.rogue.pSliceAndDice ||
					m_spells.rogue.pSliceAndDice->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pSliceAndDice = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Adrenaline Rush") == 0)
			{
				if (!m_spells.rogue.pAdrenalineRush ||
					m_spells.rogue.pAdrenalineRush->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pAdrenalineRush = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Backstab") == 0)
			{
				if (!m_spells.rogue.pBackstab ||
					m_spells.rogue.pBackstab->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pBackstab = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blade Flurry") == 0)
			{
				if (!m_spells.rogue.pBladeFlurry ||
					m_spells.rogue.pBladeFlurry->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pBladeFlurry = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Evasion") == 0)
			{
				if (!m_spells.rogue.pEvasion ||
					m_spells.rogue.pEvasion->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pEvasion = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Feint") == 0)
			{
				if (!m_spells.rogue.pFeint ||
					m_spells.rogue.pFeint->Rank[0] < pSpellEntry->Rank[0])
					m_spells.rogue.pFeint = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Gouge") == 0)
			{
			if (!m_spells.rogue.pGouge ||
				m_spells.rogue.pGouge->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pGouge = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Kick") == 0)
			{
			if (!m_spells.rogue.pKick ||
				m_spells.rogue.pKick->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pKick = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Riposte") == 0)
			{
			if (!m_spells.rogue.pRiposte ||
				m_spells.rogue.pRiposte->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pRiposte = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shiv") == 0)
			{
			if (!m_spells.rogue.pShiv ||
				m_spells.rogue.pShiv->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pShiv = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Sinister Strike") == 0)
			{
			if (!m_spells.rogue.pSinisterStrike ||
				m_spells.rogue.pSinisterStrike->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pSinisterStrike = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Sprint") == 0)
			{
			if (!m_spells.rogue.pSprint ||
				m_spells.rogue.pSprint->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pSprint = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Blind") == 0)
			{
			if (!m_spells.rogue.pBlind ||
				m_spells.rogue.pBlind->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pBlind = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Cloak of Shadow") == 0)
			{
			if (!m_spells.rogue.pCloakOfShadow ||
				m_spells.rogue.pCloakOfShadow->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pCloakOfShadow = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Disarm Traps") == 0)
			{
			if (!m_spells.rogue.pDisarmTrap ||
				m_spells.rogue.pDisarmTrap->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pDisarmTrap = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Distract") == 0)
			{
			if (!m_spells.rogue.pDistract ||
				m_spells.rogue.pDistract->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pDistract = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ghostly Strike") == 0)
			{
			if (!m_spells.rogue.pGhostlyStrike ||
				m_spells.rogue.pGhostlyStrike->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pGhostlyStrike = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Hemorrhage") == 0)
			{
			if (!m_spells.rogue.pHemorrhage ||
				m_spells.rogue.pHemorrhage->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pHemorrhage = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Pick Pocket") == 0)
			{
			if (!m_spells.rogue.pPickPoket ||
				m_spells.rogue.pPickPoket->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pPickPoket = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Premeditation") == 0)
			{
			if (!m_spells.rogue.pPremeditation ||
				m_spells.rogue.pPremeditation->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pPremeditation = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Preparation") == 0)
			{
			if (!m_spells.rogue.pPreparation ||
				m_spells.rogue.pPreparation->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pPreparation = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Sap") == 0)
			{
			if (!m_spells.rogue.pSap ||
				m_spells.rogue.pSap->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pSap = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shadowstep") == 0)
			{
			if (!m_spells.rogue.pShadowstep ||
				m_spells.rogue.pShadowstep->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pShadowstep = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Stealth") == 0)
			{
			if (!m_spells.rogue.pStealth ||
				m_spells.rogue.pStealth->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pStealth = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Vanish") == 0)
			{
			if (!m_spells.rogue.pVanish ||
				m_spells.rogue.pVanish->Rank[0] < pSpellEntry->Rank[0])
				m_spells.rogue.pVanish = pSpellEntry;
			}
			break;
		}
		case CLASS_SHAMAN:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Chain Lightning") == 0)
			{
				if (!m_spells.shaman.pChainLightning ||
					m_spells.shaman.pChainLightning->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pChainLightning = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Earth Shock") == 0)
			{
				if (!m_spells.shaman.pEarthShock ||
					m_spells.shaman.pEarthShock->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pEarthShock = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Earthbind Totem") == 0)
			{
				if (!m_spells.shaman.pEarthbindTotem ||
					m_spells.shaman.pEarthbindTotem->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pEarthbindTotem = pSpellEntry;
					pEarthbindTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Elemental Mastery") == 0)
			{
				if (!m_spells.shaman.pElementalMastery ||
					m_spells.shaman.pElementalMastery->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pElementalMastery = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Fire Elemental Totem") == 0)
			{
				if (!m_spells.shaman.pFireElementalTotem ||
					m_spells.shaman.pFireElementalTotem->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pFireElementalTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Fire Nova Totem") == 0)
			{
				if (!m_spells.shaman.pFireNovaTotem ||
					m_spells.shaman.pFireNovaTotem->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pFireNovaTotem = pSpellEntry;
					pFireNovaTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Flame Shock") == 0)
			{
				if (!m_spells.shaman.pFlameShock ||
					m_spells.shaman.pFlameShock->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pFlameShock = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Frost Shock") == 0)
			{
				if (!m_spells.shaman.pFrostShock ||
					m_spells.shaman.pFrostShock->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pFrostShock = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Lightning Bolt") == 0)
			{
				if (!m_spells.shaman.pLightningBolt ||
					m_spells.shaman.pLightningBolt->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pLightningBolt = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Magma Totem") == 0)
			{
				if (!m_spells.shaman.pMagmaTotem ||
					m_spells.shaman.pMagmaTotem->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pMagmaTotem = pSpellEntry;
					pMagmaTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Purge") == 0)
			{
				if (!m_spells.shaman.pPurge ||
					m_spells.shaman.pPurge->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pPurge = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Searing Totem") == 0)
			{
				if (!m_spells.shaman.pSearingTotem ||
					m_spells.shaman.pSearingTotem->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pSearingTotem = pSpellEntry;
					pSearingTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Stoneclaw Totem") == 0)
			{
				if (!m_spells.shaman.pStoneclawTotem ||
					m_spells.shaman.pStoneclawTotem->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pStoneclawTotem = pSpellEntry;
					pStoneclawtotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Totem of Wrath") == 0)
			{
				if (!m_spells.shaman.pTotemOfWrath ||
					m_spells.shaman.pTotemOfWrath->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pTotemOfWrath = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Astral Recall") == 0)
			{
				if (!m_spells.shaman.pAstralRecall ||
					m_spells.shaman.pAstralRecall->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pAstralRecall = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Earth Elemental Totem") == 0)
			{
				if (!m_spells.shaman.pEarthElementalTotem ||
					m_spells.shaman.pEarthElementalTotem->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pEarthElementalTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Far Sight") == 0)
			{
				if (!m_spells.shaman.pFarSight ||
					m_spells.shaman.pFarSight->Rank[0] < pSpellEntry->Rank[0])
					m_spells.shaman.pFarSight = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Fire Resistance Totem") == 0)
			{
			if (!m_spells.shaman.pFireResistanceTotem ||
				m_spells.shaman.pFireResistanceTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pFireResistanceTotem = pSpellEntry;
				pFireResistanceTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Flametongue Totem") == 0)
			{
			if (!m_spells.shaman.pFlametongueTotem ||
				m_spells.shaman.pFlametongueTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pFlametongueTotem = pSpellEntry;
				pFlametongueTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Flametongue Weapon") == 0)
			{
			if (!m_spells.shaman.pFlametongueWeapon ||
				m_spells.shaman.pFlametongueWeapon->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pFlametongueWeapon = pSpellEntry;
				pFlametongueWeapon = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Frost Resistance Totem") == 0)
			{
			if (!m_spells.shaman.pFrostResistanceTotem ||
				m_spells.shaman.pFrostResistanceTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pFrostResistanceTotem = pSpellEntry;
				pFrostResistanceTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Frostbrand Weapon") == 0)
			{
			if (!m_spells.shaman.pFrostbrandWeapon ||
				m_spells.shaman.pFrostbrandWeapon->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pFrostbrandWeapon = pSpellEntry;
				pFrostbrandWeapon = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ghost Wolf") == 0)
			{
			if (!m_spells.shaman.pGhostWolf ||
				m_spells.shaman.pGhostWolf->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pGhostWolf = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Grace of Air Totem") == 0)
			{
			if (!m_spells.shaman.pGraceofAirTotem ||
				m_spells.shaman.pGraceofAirTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pGraceofAirTotem = pSpellEntry;
				pGraceOfAirTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Heroism") == 0)
			{
			if (!m_spells.shaman.pHeroism ||
				m_spells.shaman.pHeroism->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pHeroism = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Lightning Shield") == 0)
			{
			if (!m_spells.shaman.pLightningShield ||
				m_spells.shaman.pLightningShield->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pLightningShield = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Nature Resistance Totem") == 0)
			{
			if (!m_spells.shaman.pNatureResistanceTotem ||
				m_spells.shaman.pNatureResistanceTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pNatureResistanceTotem = pSpellEntry;
				pNatureResistanceTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Rockbiter Weapon") == 0)
			{
			if (!m_spells.shaman.pRockbiterWeapon ||
				m_spells.shaman.pRockbiterWeapon->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pRockbiterWeapon = pSpellEntry;
				pRockbiterWeapon = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Sentry Totem") == 0)
			{
			if (!m_spells.shaman.pSentryTotem ||
				m_spells.shaman.pSentryTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pSentryTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shamanistic Rage") == 0)
			{
			if (!m_spells.shaman.pShamanisticRage ||
				m_spells.shaman.pShamanisticRage->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pShamanisticRage = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Stoneskin Totem") == 0)
			{
			if (!m_spells.shaman.pStoneskinTotem ||
				m_spells.shaman.pStoneskinTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pStoneskinTotem = pSpellEntry;
				pStoneskinTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Stormstrike") == 0)
			{
			if (!m_spells.shaman.pStormstrike ||
				m_spells.shaman.pStormstrike->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pStormstrike = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Strenght of Earth Totem") == 0)
			{
			if (!m_spells.shaman.pStrenghtOfEarthTotem ||
				m_spells.shaman.pStrenghtOfEarthTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pStrenghtOfEarthTotem = pSpellEntry;
				pStrengthOfEarthTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Water Breathing") == 0)
			{
			if (!m_spells.shaman.pWaterBreathing ||
				m_spells.shaman.pWaterBreathing->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pWaterBreathing = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Water Walking") == 0)
			{
			if (!m_spells.shaman.pWaterWalking ||
				m_spells.shaman.pWaterWalking->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pWaterWalking = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Windfury Totem") == 0)
			{
			if (!m_spells.shaman.pWindFuryTotem ||
				m_spells.shaman.pWindFuryTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pWindFuryTotem = pSpellEntry;
				pWindfuryTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Windfury Weapon") == 0)
			{
			if (!m_spells.shaman.pWindFuryWeapon ||
				m_spells.shaman.pWindFuryWeapon->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pWindFuryWeapon = pSpellEntry;
				pWindfuryWeapon = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Windwall Totem") == 0)
			{
			if (!m_spells.shaman.pWindwallTotem ||
				m_spells.shaman.pWindwallTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pWindwallTotem = pSpellEntry;
				pWindwallTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Wrath of Air Totem") == 0)
			{
			if (!m_spells.shaman.pWrathofAirTotem ||
				m_spells.shaman.pWrathofAirTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pWrathofAirTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ancestral Spirit") == 0)
			{
			if (!m_spells.shaman.pAncestralSpirit ||
				m_spells.shaman.pAncestralSpirit->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pAncestralSpirit = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Chain Heal") == 0)
			{
			if (!m_spells.shaman.pChainHeal ||
				m_spells.shaman.pChainHeal->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pChainHeal = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Cure Disease") == 0)
			{
			if (!m_spells.shaman.pCureDisease ||
				m_spells.shaman.pCureDisease->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pCureDisease = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Cure Poison") == 0)
			{
			if (!m_spells.shaman.pCurePoison ||
				m_spells.shaman.pCurePoison->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pCurePoison = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Disease Cleansing Totem") == 0)
			{
			if (!m_spells.shaman.pDiseaseCleansingTotem ||
				m_spells.shaman.pDiseaseCleansingTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pDiseaseCleansingTotem = pSpellEntry;
				pDiseaseCleansingTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Earth Shield") == 0)
			{
			if (!m_spells.shaman.pEarthShield ||
				m_spells.shaman.pEarthShield->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pEarthShield = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Healing Stream Totem") == 0)
			{
			if (!m_spells.shaman.pHealingStreamTotem ||
				m_spells.shaman.pHealingStreamTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pHealingStreamTotem = pSpellEntry;
				pHealingStreamTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Healing Wave") == 0)
			{
			if (!m_spells.shaman.pHealingWave ||
				m_spells.shaman.pHealingWave->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pHealingWave = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Lesser Healing Wave") == 0)
			{
			if (!m_spells.shaman.pLesserHealingWave ||
				m_spells.shaman.pLesserHealingWave->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pLesserHealingWave = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mana Spring Totem") == 0)
			{
			if (!m_spells.shaman.pManaSpringTotem ||
				m_spells.shaman.pManaSpringTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pManaSpringTotem = pSpellEntry;
				pManaSpringTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Mana Tide Totem") == 0)
			{
			if (!m_spells.shaman.pManaTideTotem ||
				m_spells.shaman.pManaTideTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pManaTideTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Nature's Swiftness") == 0)
			{
			if (!m_spells.shaman.pNaturesSwiftness ||
				m_spells.shaman.pNaturesSwiftness->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pNaturesSwiftness = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Poison Cleansing Totem") == 0)
			{
			if (!m_spells.shaman.pPoisonCleansingTotem ||
				m_spells.shaman.pPoisonCleansingTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pPoisonCleansingTotem = pSpellEntry;
				pPoisonCleansingTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Reincarnation") == 0)
			{
			if (!m_spells.shaman.pReincarnation ||
				m_spells.shaman.pReincarnation->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pReincarnation = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Totemic Call") == 0)
			{
			if (!m_spells.shaman.pTotemicCall ||
				m_spells.shaman.pTotemicCall->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pTotemicCall = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Tranquil Air Totem") == 0)
			{
			if (!m_spells.shaman.pTranquilAirTotem ||
				m_spells.shaman.pTranquilAirTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pTranquilAirTotem = pSpellEntry;
				pTranquilAirTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Tremor Totem") == 0)
			{
			if (!m_spells.shaman.pTremorTotem ||
				m_spells.shaman.pTremorTotem->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pTremorTotem = pSpellEntry;
				pTremorTotem = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Water Shield") == 0)
			{
			if (!m_spells.shaman.pWaterShield ||
				m_spells.shaman.pWaterShield->Rank[0] < pSpellEntry->Rank[0])
				m_spells.shaman.pWaterShield = pSpellEntry;
			}
			break;
		}
		case CLASS_WARLOCK:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Amplify Curse") == 0)
			{
				if (!m_spells.warlock.pAmplifyCurse ||
					m_spells.warlock.pAmplifyCurse->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pAmplifyCurse = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Corruption") == 0)
			{
				if (!m_spells.warlock.pCorruption ||
					m_spells.warlock.pCorruption->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pCorruption = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Curse of Agony") == 0)
			{
				if (!m_spells.warlock.pCurseOfAgony ||
					m_spells.warlock.pCurseOfAgony->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pCurseOfAgony = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Curse of Doom") == 0)
			{
				if (!m_spells.warlock.pCurseOfDoom ||
					m_spells.warlock.pCurseOfDoom->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pCurseOfDoom = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Curse of Exhaustion") == 0)
			{
				if (!m_spells.warlock.pCurseOfExhaustion ||
					m_spells.warlock.pCurseOfExhaustion->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pCurseOfExhaustion = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Curse of Recklessness") == 0)
			{
				if (!m_spells.warlock.pCurseOfRecklessness ||
					m_spells.warlock.pCurseOfRecklessness->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pCurseOfRecklessness = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Curse of Idiocy") == 0)
			{
				if (!m_spells.warlock.pCurseOfIdiocy ||
					m_spells.warlock.pCurseOfIdiocy->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pCurseOfIdiocy = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Curse of the Elements") == 0)
			{
				if (!m_spells.warlock.pCurseOfTheElements ||
					m_spells.warlock.pCurseOfTheElements->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pCurseOfTheElements = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Curse of Tongues") == 0)
			{
				if (!m_spells.warlock.pCurseOfTongues ||
					m_spells.warlock.pCurseOfTongues->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pCurseOfTongues = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Curse of Weakness") == 0)
			{
				if (!m_spells.warlock.pCurseOfWeakness ||
					m_spells.warlock.pCurseOfWeakness->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pCurseOfWeakness = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Dark Pact") == 0)
			{
				if (!m_spells.warlock.pDarkPact ||
					m_spells.warlock.pDarkPact->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pDarkPact = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Death Coil") == 0)
			{
				if (!m_spells.warlock.pDeathCoil ||
					m_spells.warlock.pDeathCoil->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pDeathCoil = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Drain Life") == 0)
			{
				if (!m_spells.warlock.pDrainLife ||
					m_spells.warlock.pDrainLife->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pDrainLife = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Drain Mana") == 0)
			{
				if (!m_spells.warlock.pDrainMana ||
					m_spells.warlock.pDrainMana->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pDrainMana = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Drain Soul") == 0)
			{
				if (!m_spells.warlock.pDrainSoul ||
					m_spells.warlock.pDrainSoul->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pDrainSoul = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Fear") == 0)
			{
				if (!m_spells.warlock.pFear ||
					m_spells.warlock.pFear->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pFear = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Howl of Terror") == 0)
			{
				if (!m_spells.warlock.pHowlofTerror ||
					m_spells.warlock.pHowlofTerror->Rank[0] < pSpellEntry->Rank[0])
					m_spells.warlock.pHowlofTerror = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Life Tap") == 0)
			{
			if (!m_spells.warlock.pHowlofTerror ||
				m_spells.warlock.pHowlofTerror->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pHowlofTerror = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Seed of Corruption") == 0)
			{
			if (!m_spells.warlock.pSeedOfCorruption ||
				m_spells.warlock.pSeedOfCorruption->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSeedOfCorruption = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Siphon Life") == 0)
			{
			if (!m_spells.warlock.pSiphonLife ||
				m_spells.warlock.pSiphonLife->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSiphonLife = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Unstable Affliction") == 0)
			{
			if (!m_spells.warlock.pUnstableAffliction ||
				m_spells.warlock.pUnstableAffliction->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pUnstableAffliction = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Banish") == 0)
			{
			if (!m_spells.warlock.pBanish ||
				m_spells.warlock.pBanish->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pBanish = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Create Firestone") == 0)
			{
			if (!m_spells.warlock.pCreateFirestone ||
				m_spells.warlock.pCreateFirestone->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pCreateFirestone = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Create Healthstone") == 0)
			{
			if (!m_spells.warlock.pCreateHealthstone ||
				m_spells.warlock.pCreateHealthstone->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pCreateHealthstone = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Create Soulstone") == 0)
			{
			if (!m_spells.warlock.pCreateSoulstone ||
				m_spells.warlock.pCreateSoulstone->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pCreateSoulstone = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Create Spellstone") == 0)
			{
			if (!m_spells.warlock.pCreateSpellstone ||
				m_spells.warlock.pCreateSpellstone->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pCreateSpellstone = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Demon Armor") == 0)
			{
			if (!m_spells.warlock.pDemonArmor ||
				m_spells.warlock.pDemonArmor->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pDemonArmor = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Demonic Sacrifice") == 0)
			{
			if (!m_spells.warlock.pDemonicSacrifice ||
				m_spells.warlock.pDemonicSacrifice->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pDemonicSacrifice = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Detect Invisibility") == 0)
			{
			if (!m_spells.warlock.pDetectInvisibility ||
				m_spells.warlock.pDetectInvisibility->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pDetectInvisibility = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Enslave Demon") == 0)
			{
			if (!m_spells.warlock.pEnslaveDemon ||
				m_spells.warlock.pEnslaveDemon->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pEnslaveDemon = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Enslave Demon") == 0)
			{
			if (!m_spells.warlock.pEnslaveDemon ||
				m_spells.warlock.pEnslaveDemon->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pEnslaveDemon = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Eye of Kilrogg") == 0)
			{
			if (!m_spells.warlock.pEyeOfKilrogg ||
				m_spells.warlock.pEyeOfKilrogg->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pEyeOfKilrogg = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Fel Domination") == 0)
			{
			if (!m_spells.warlock.pFelDomination ||
				m_spells.warlock.pFelDomination->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pFelDomination = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Health Funnel") == 0)
			{
			if (!m_spells.warlock.pHealthFunnel ||
				m_spells.warlock.pHealthFunnel->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pHealthFunnel = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ritual of Doom") == 0)
			{
			if (!m_spells.warlock.pRitualOfDoom ||
				m_spells.warlock.pRitualOfDoom->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pRitualOfDoom = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ritual of Soul") == 0)
			{
			if (!m_spells.warlock.pRitualOfSouls ||
				m_spells.warlock.pRitualOfSouls->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pRitualOfSouls = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Ritual of Summoning") == 0)
			{
			if (!m_spells.warlock.pRitualOfSummoning ||
				m_spells.warlock.pRitualOfSummoning->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pRitualOfSummoning = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Sense Demons") == 0)
			{
			if (!m_spells.warlock.pSenseDemons ||
				m_spells.warlock.pSenseDemons->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSenseDemons = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shadow Ward") == 0)
			{
			if (!m_spells.warlock.pShadowWard ||
				m_spells.warlock.pShadowWard->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pShadowWard = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Soulshatter") == 0)
			{
			if (!m_spells.warlock.pSoulShatter ||
				m_spells.warlock.pSoulShatter->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSoulShatter = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Summon Felguard") == 0)
			{
			if (!m_spells.warlock.pSummonFelguard ||
				m_spells.warlock.pSummonFelguard->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSummonFelguard = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Summon Felhunter") == 0)
			{
			if (!m_spells.warlock.pSummonFelhunter ||
				m_spells.warlock.pSummonFelhunter->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSummonFelhunter = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Summon Felsteed") == 0)
			{
			if (!m_spells.warlock.pSummonFelsteed ||
				m_spells.warlock.pSummonFelsteed->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSummonFelsteed = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Summon Imp") == 0)
			{
			if (!m_spells.warlock.pSummonImp ||
				m_spells.warlock.pSummonImp->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSummonImp = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Summon Succubus") == 0)
			{
			if (!m_spells.warlock.pSummonSuccubus ||
				m_spells.warlock.pSummonSuccubus->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSummonSuccubus = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Summon Voidwalker") == 0)
			{
			if (!m_spells.warlock.pSummonVoidwalker ||
				m_spells.warlock.pSummonVoidwalker->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSummonVoidwalker = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Summon Incubus") == 0)
			{
			if (!m_spells.warlock.pSummonIncubus ||
				m_spells.warlock.pSummonIncubus->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSummonIncubus = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Conflagrate") == 0)
			{
			if (!m_spells.warlock.pConflagrate ||
				m_spells.warlock.pConflagrate->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pConflagrate = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Hellfire") == 0)
			{
			if (!m_spells.warlock.pHellfire ||
				m_spells.warlock.pHellfire->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pHellfire = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Immolate") == 0)
			{
			if (!m_spells.warlock.pImmolate ||
				m_spells.warlock.pImmolate->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pImmolate = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Incinerate") == 0)
			{
			if (!m_spells.warlock.pIncinerate ||
				m_spells.warlock.pIncinerate->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pIncinerate = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Rain of Fire") == 0)
			{
			if (!m_spells.warlock.pRainOfFire ||
				m_spells.warlock.pRainOfFire->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pRainOfFire = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Searing Pain") == 0)
			{
			if (!m_spells.warlock.pSearingPain ||
				m_spells.warlock.pSearingPain->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSearingPain = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shadow Bolt") == 0)
			{
			if (!m_spells.warlock.pShadowBolt ||
				m_spells.warlock.pShadowBolt->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pShadowBolt = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shadowburn") == 0)
			{
			if (!m_spells.warlock.pShadowburn ||
				m_spells.warlock.pShadowburn->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pShadowburn = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Shadowfury") == 0)
			{
			if (!m_spells.warlock.pShadowfury ||
				m_spells.warlock.pShadowfury->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pShadowfury = pSpellEntry;
			}
			else if (std::strcmp(pSpellEntry->SpellName[0], "Soul Fire") == 0)
			{
			if (!m_spells.warlock.pSoulFire ||
				m_spells.warlock.pSoulFire->Rank[0] < pSpellEntry->Rank[0])
				m_spells.warlock.pSoulFire = pSpellEntry;
			}
			break;
		}
		}
		for (uint32 i = 0; i < MAX_SPELL_EFFECTS; i++)
		{
			switch (pSpellEntry->Effect[i])
			{
				case SPELL_EFFECT_HEAL:
				{
					switch (me->getClass())
					{
						case CLASS_PALADIN:
						{
							if (me->GetLevel() >= 38)
							{
								if (pSpellEntry->SpellName[0] == "Holy Light")
								{
									if (pSpellEntry->Rank[0] == "Rank 6" ||
										pSpellEntry->Rank[0] == "Rank 7" ||
										pSpellEntry->Rank[0] == "Rank 8" ||
										pSpellEntry->Rank[0] == "Rank 9")
										spellListDirectHeal.insert(pSpellEntry);
								}
							}
							else if (me->GetLevel() >= 20 && me->GetLevel() <= 37)
							{
								if (pSpellEntry->SpellName[0] == "Holy Light")
								{
									if (pSpellEntry->Rank[0] == "Rank 3" ||
										pSpellEntry->Rank[0] == "Rank 4" ||
										pSpellEntry->Rank[0] == "Rank 5")
										spellListDirectHeal.insert(pSpellEntry);
								}
							}
							else
							{
								spellListDirectHeal.insert(pSpellEntry);
							}

							break;
						}
						case CLASS_DRUID:
						{
							if (pSpellEntry->SpellName[0] == "Healing Touch")
							{
								if (me->GetLevel() >= 60)
								{
									if (pSpellEntry->Rank[0] != "Rank 1" &&
										pSpellEntry->Rank[0] != "Rank 2")
										spellListDirectHeal.insert(pSpellEntry);
								}
								else
								{
									spellListDirectHeal.insert(pSpellEntry);
								}
							}
							else if (pSpellEntry->SpellName[0] == "Regrowth")
							{
								spellListDirectHeal.insert(pSpellEntry);
								break;
							}
						}
						case CLASS_PRIEST:
						{
							if (me->GetLevel() >= 30)
							{
								if (pSpellEntry->SpellName[0] == "Flash Heal" ||
									pSpellEntry->SpellName[0] == "Greater Heal" ||
									pSpellEntry->SpellName[0] == "Heal")
								{
									spellListDirectHeal.insert(pSpellEntry);
									break;
								}
							}
							else
							{
								if (pSpellEntry->SpellName[0] == "Flash Heal" ||
									pSpellEntry->SpellName[0] == "Greater Heal" ||
									pSpellEntry->SpellName[0] == "Heal" ||
									pSpellEntry->SpellName[0] == "Lesser Heal")
								{
									spellListDirectHeal.insert(pSpellEntry);
									break;
								}
							}
						}
						case CLASS_SHAMAN:
						{
							if (pSpellEntry->SpellName[0] == "Healing Wave")
							{
								if (me->GetLevel() >= 60)
								{
									if (pSpellEntry->Rank[0] != "Rank 1")
										spellListDirectHeal.insert(pSpellEntry);
								}
								else
								{
									spellListDirectHeal.insert(pSpellEntry);
								}
							}
							else if (pSpellEntry->SpellName[0] == "Lesser Healing Wave" ||
								pSpellEntry->SpellName[0] == "Chain Heal")
							{
								spellListDirectHeal.insert(pSpellEntry);
								break;
							}
						}
					}

					break;
				}
				case SPELL_EFFECT_SCRIPT_EFFECT:
				{
					switch (me->getClass())
					{
						case CLASS_PALADIN:
						{
							if (pSpellEntry->SpellName[0] == "Flash of Light")
								spellListDirectHeal.insert(pSpellEntry);

							break;
						}
					}

					break;
				}
				case SPELL_EFFECT_ATTACK_ME:
				{
					spellListTaunt.push_back(pSpellEntry);
					break;
				}
				case SPELL_EFFECT_RESURRECT:
				case SPELL_EFFECT_RESURRECT_NEW:
					if (pSpellEntry->SpellName[0] != "Rebirth")
					{
						m_resurrectionSpell = pSpellEntry;
						break;
					}
				case SPELL_EFFECT_APPLY_AURA:
				{
					switch (pSpellEntry->EffectApplyAuraName[i])
					{
						case SPELL_AURA_PERIODIC_HEAL:
						{
							spellListPeriodicHeal.insert(pSpellEntry);
							break;
						}
						case SPELL_AURA_MOD_TAUNT:
						{
							spellListTaunt.push_back(pSpellEntry);
							break;
						}
					}
					break;
				}
			}
		}

		switch (me->getClass())
		{
			case CLASS_SHAMAN:
			{
				std::vector<SpellEntry const*> airTotems;
				if (pGraceOfAirTotem)
					airTotems.push_back(pGraceOfAirTotem);
				if (pNatureResistanceTotem)
					airTotems.push_back(pNatureResistanceTotem);
				if (pWindfuryTotem)
					airTotems.push_back(pWindfuryTotem);
				if (pWindwallTotem)
					airTotems.push_back(pWindwallTotem);
				if (pTranquilAirTotem)
					airTotems.push_back(pTranquilAirTotem);
				if (!airTotems.empty())
					m_spells.shaman.pAirTotem = SelectRandomContainerElement(airTotems);

				std::vector<SpellEntry const*> earthTotems;
				if (pEarthbindTotem)
					earthTotems.push_back(pEarthbindTotem);
				if (pStoneclawtotem)
					earthTotems.push_back(pStoneclawtotem);
				if (pStoneskinTotem)
					earthTotems.push_back(pStoneskinTotem);
				if (pStrengthOfEarthTotem)
					earthTotems.push_back(pStrengthOfEarthTotem);
				if (pTremorTotem)
					earthTotems.push_back(pTremorTotem);
				if (!earthTotems.empty())
					m_spells.shaman.pEarthTotem = SelectRandomContainerElement(earthTotems);

				std::vector<SpellEntry const*> fireTotems;
				if (pFireNovaTotem)
					fireTotems.push_back(pFireNovaTotem);
				if (pMagmaTotem)
					fireTotems.push_back(pMagmaTotem);
				if (pSearingTotem)
					fireTotems.push_back(pSearingTotem);
				if (pFlametongueTotem)
					fireTotems.push_back(pFlametongueTotem);
				if (pFrostResistanceTotem)
					fireTotems.push_back(pFrostResistanceTotem);
				if (!fireTotems.empty())
					m_spells.shaman.pFireTotem = SelectRandomContainerElement(fireTotems);

				std::vector<SpellEntry const*> waterTotems;
				if (pFireResistanceTotem)
					waterTotems.push_back(pFireResistanceTotem);
				if (pDiseaseCleansingTotem)
					waterTotems.push_back(pDiseaseCleansingTotem);
				if (pHealingStreamTotem)
					waterTotems.push_back(pHealingStreamTotem);
				if (pManaSpringTotem)
					waterTotems.push_back(pManaSpringTotem);
				if (pPoisonCleansingTotem)
					waterTotems.push_back(pPoisonCleansingTotem);
				if (!waterTotems.empty())
					m_spells.shaman.pWaterTotem = SelectRandomContainerElement(waterTotems);

				if (pWindfuryWeapon && m_role == ROLE_MELEE_DPS)
					m_spells.shaman.pWeaponBuff = pWindfuryWeapon;
				else
				{
					std::vector<SpellEntry const*> weaponBuffs;
					if (pWindfuryWeapon)
						weaponBuffs.push_back(pWindfuryWeapon);
					if (pRockbiterWeapon)
						weaponBuffs.push_back(pRockbiterWeapon);
					if (pFrostbrandWeapon)
						weaponBuffs.push_back(pFrostbrandWeapon);
					if (pFlametongueWeapon)
						weaponBuffs.push_back(pFlametongueWeapon);
					if (!weaponBuffs.empty())
						m_spells.shaman.pWeaponBuff = SelectRandomContainerElement(weaponBuffs);
				}

				break;
			}
			case CLASS_MAGE:
			{
				if (!m_spells.mage.pIceArmor && pFrostArmor)
					m_spells.mage.pIceArmor = pFrostArmor;

				std::vector<SpellEntry const*> polymorph;
				if (pPolymorphSheep)
					polymorph.push_back(pPolymorphSheep);
				if (pPolymorphCow)
					polymorph.push_back(pPolymorphCow);
				if (pPolymorphPig)
					polymorph.push_back(pPolymorphPig);
				if (pPolymorphTurtle)
					polymorph.push_back(pPolymorphTurtle);
				if (!polymorph.empty())
					m_spells.mage.pPolymorph = SelectRandomContainerElement(polymorph);

				break;
			}
			case CLASS_ROGUE:
			{
				SpellEntry const* pInstantPoison = nullptr;
				if (me->GetLevel() >= 60)
					pInstantPoison = sSpellMgr.GetSpellEntry(SPELL_INSTANT_POISON_VI);
				else if (me->GetLevel() >= 52)
					pInstantPoison = sSpellMgr.GetSpellEntry(SPELL_INSTANT_POISON_V);
				else if (me->GetLevel() >= 44)
					pInstantPoison = sSpellMgr.GetSpellEntry(SPELL_INSTANT_POISON_IV);
				else if (me->GetLevel() >= 36)
					pInstantPoison = sSpellMgr.GetSpellEntry(SPELL_INSTANT_POISON_III);
				else if (me->GetLevel() >= 28)
					pInstantPoison = sSpellMgr.GetSpellEntry(SPELL_INSTANT_POISON_II);
				else if (me->GetLevel() >= 20)
					pInstantPoison = sSpellMgr.GetSpellEntry(SPELL_INSTANT_POISON_I);

				SpellEntry const* pDeadlyPoison = nullptr;
				if (me->GetLevel() >= 60)
					pDeadlyPoison = sSpellMgr.GetSpellEntry(SPELL_DEADLY_POISON_V);
				else if (me->GetLevel() >= 54)
					pDeadlyPoison = sSpellMgr.GetSpellEntry(SPELL_DEADLY_POISON_IV);
				else if (me->GetLevel() >= 46)
					pDeadlyPoison = sSpellMgr.GetSpellEntry(SPELL_DEADLY_POISON_III);
				else if (me->GetLevel() >= 38)
					pDeadlyPoison = sSpellMgr.GetSpellEntry(SPELL_DEADLY_POISON_II);
				else if (me->GetLevel() >= 30)
					pDeadlyPoison = sSpellMgr.GetSpellEntry(SPELL_DEADLY_POISON_I);
				else if (me->GetLevel() >= 20)
					pDeadlyPoison = sSpellMgr.GetSpellEntry(SPELL_INSTANT_POISON_I);

				if (me->IsTier <= T5D)
				{
					if (pInstantPoison)
					{
						m_spells.rogue.pMainHandPoison = pInstantPoison;
						m_spells.rogue.pOffHandPoison = pInstantPoison;
					}
				}
				else
				{
					if (pInstantPoison && pDeadlyPoison)
					{
						m_spells.rogue.pMainHandPoison = pDeadlyPoison;
						m_spells.rogue.pOffHandPoison = pInstantPoison;
					}
				}

				break;
			}
		}

		switch (me->getRace())
		{
		case RACE_HUMAN:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Perception") == 0)
			{
				if (!m_spells.racial.pPerception)
					m_spells.racial.pPerception = pSpellEntry;
			}
			break;
		}
		case RACE_DWARF:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Stoneform") == 0)
			{
				if (!m_spells.racial.pStoneform)
					m_spells.racial.pStoneform = pSpellEntry;
			}
			break;
		}
		case RACE_NIGHTELF:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Shadowmeld") == 0)
			{
				if (!m_spells.racial.pShadowmeld)
					m_spells.racial.pShadowmeld = pSpellEntry;
			}
			break;
		}
		case RACE_GNOME:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Escape Artist") == 0)
			{
				if (!m_spells.racial.pEscapeArtist)
					m_spells.racial.pEscapeArtist = pSpellEntry;
			}
			break;
		}
		case RACE_DRAENEI:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Gift of the Naaru") == 0)
			{
				if (!m_spells.racial.pGiftOfTheNaaru)
					m_spells.racial.pGiftOfTheNaaru = pSpellEntry;
			}
			break;
		}
		case RACE_ORC:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Blood Fury") == 0)
			{
				if (!m_spells.racial.pBloodFury)
					m_spells.racial.pBloodFury = pSpellEntry;
			}
			break;
		}
		case RACE_UNDEAD:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Will of the Forsaken") == 0)
			{
				if (!m_spells.racial.pWillOfTheForsaken)
					m_spells.racial.pWillOfTheForsaken = pSpellEntry;
			}
			break;
		}
		case RACE_TAUREN:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "War Stomp") == 0)
			{
				if (!m_spells.racial.pWarStomp)
					m_spells.racial.pWarStomp = pSpellEntry;
			}
			break;
		}
		case RACE_TROLL:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Berserking") == 0)
			{
				if (!m_spells.racial.pBerserking)
					m_spells.racial.pBerserking = pSpellEntry;
			}
			break;
		}
		case RACE_BLOODELF:
		{
			if (std::strcmp(pSpellEntry->SpellName[0], "Arcane Torrent") == 0)
			{
				if (!m_spells.racial.pArcaneTorrent)
					m_spells.racial.pArcaneTorrent = pSpellEntry;
			}
			break;
		}
		}
	}
}

void CombatBotBaseAI::AddAllSpellReagents(Player* pTarget)
{
	for (const auto& pSpell : m_spells.raw.spells)
	{
		if (pSpell)
		{
			for (const auto& reagent : pSpell->Reagent)
			{
				if (reagent && !pTarget->HasItemCount(reagent, 1))
					pTarget->StoreNewItemInBestSlots(reagent, 1);
			}
			for (const auto& totem : pSpell->Totem)
			{
				if (totem && !pTarget->HasItemCount(totem, 1))
					pTarget->StoreNewItemInBestSlots(totem, 1);
			}
		}
	}

	// Add Thieve's Tools to Rogues
	if (pTarget->getClass() == CLASS_ROGUE && pTarget->GetLevel() >= 15 &&
		!pTarget->HasItemCount(5060, 1))
		pTarget->StoreNewItemInBestSlots(5060, 1);
}

void CombatBotBaseAI::SetMountSelection()
{
	if (me->GetTeam() == ALLIANCE)
	{
		// 280% Speed Flying Mounts
		std::vector<uint32> mounts = { 32292, 32242, 32289, 32290 };
		mount_flying_spell_id_280 = SelectRandomContainerElement(mounts);
		// 60% Speed Mounts
		mounts = { 32240, 32239, 32235 };
		mount_flying_spell_id_60 = SelectRandomContainerElement(mounts);
	}
	else if (me->GetTeam() == HORDE)
	{
		// 280% Speed Flying Mounts
		std::vector<uint32> mounts = { 32246, 32295, 32296, 32297 };
		mount_flying_spell_id_280 = SelectRandomContainerElement(mounts);
		// 60% Speed Mounts
		mounts = { 32244, 32245, 32243 };
		mount_flying_spell_id_60 = SelectRandomContainerElement(mounts);
	}
	if (me->getClass() == CLASS_PALADIN)
	{
		mount_spell_id_60 = 13819;
		mount_spell_id_100 = 23214;
		return;
	}
	else if (me->getClass() == CLASS_WARLOCK)
	{
		mount_spell_id_60 = 5784;
		mount_spell_id_100 = 23161;
		return;
	}
	switch (me->getRace())
	{
	case RACE_HUMAN:
	{
		// 100% Speed Mounts
		std::vector<uint32> mounts = { 16082, 16083, 23229, 23227, 23228 };
		mount_spell_id_100 = SelectRandomContainerElement(mounts);
		// 60% Speed Mounts
		mounts = { 471, 468, 472, 458, 6648 };
		mount_spell_id_60 = SelectRandomContainerElement(mounts);
		break;
	}
	case RACE_DWARF:
	{
		// 100% Speed Mounts
		std::vector<uint32> mounts = { 17461, 17460, 23240, 23239, 23238 };
		mount_spell_id_100 = SelectRandomContainerElement(mounts);
		// 60% Speed Mounts
		mounts = { 6896, 6897, 6899, 6777, 6898 };
		mount_spell_id_60 = SelectRandomContainerElement(mounts);
		break;
	}
	case RACE_NIGHTELF:
	{
		// 100% Speed Mounts
		std::vector<uint32> mounts = { 16056, 24252, 17229, 16055, 23220, 23338, 23219, 23221 };
		mount_spell_id_100 = SelectRandomContainerElement(mounts);
		// 60% Speed Mounts
		mounts = { 16059, 10790, 16060, 10788, 10793, 8394, 10789 };
		mount_spell_id_60 = SelectRandomContainerElement(mounts);
		break;
	}
	case RACE_GNOME:
	{
		// 100% Speed Mounts
		std::vector<uint32> mounts = { 15779, 17459, 23222, 23223, 23225 };
		mount_spell_id_100 = SelectRandomContainerElement(mounts);
		// 60% Speed Mounts
		mounts = { 17455, 17456, 17458, 17454, 10873, 17453, 10969 };
		mount_spell_id_60 = SelectRandomContainerElement(mounts);
		break;
	}
	case RACE_ORC:
	{
		// 100% Speed Mounts
		std::vector<uint32> mounts = { 16081, 16080, 23251, 23252, 23250 };
		mount_spell_id_100 = SelectRandomContainerElement(mounts);
		// 60% Speed Mounts
		mounts = { 578, 581, 459, 580, 6653, 6654 };
		mount_spell_id_60 = SelectRandomContainerElement(mounts);
		break;
	}
	case RACE_UNDEAD:
	{
		// 100% Speed Mounts
		std::vector<uint32> mounts = { 17481, 23246, 17465 };
		mount_spell_id_100 = SelectRandomContainerElement(mounts);
		// 60% Speed Mounts
		mounts = { 8980, 17462, 17464, 17463 };
		mount_spell_id_60 = SelectRandomContainerElement(mounts);
		break;
	}
	case RACE_TAUREN:
	{
		// 100% Speed Mounts
		std::vector<uint32> mounts = { 18992, 18991, 23247, 23248, 23249 };
		mount_spell_id_100 = SelectRandomContainerElement(mounts);
		// 60% Speed Mounts
		mounts = { 18989, 18990 };
		mount_spell_id_60 = SelectRandomContainerElement(mounts);
		break;
	}
	case RACE_TROLL:
	{
		// 100% Speed Mounts
		std::vector<uint32> mounts = { 16084, 24242, 17450, 23243, 23242, 23241 };
		mount_spell_id_100 = SelectRandomContainerElement(mounts);
		// 60% Speed Mounts
		mounts = { 10798, 10795, 10799, 10796, 8395 };
		mount_spell_id_60 = SelectRandomContainerElement(mounts);
		break;
	}
	}
}

void CombatBotBaseAI::ClearInventory(Player* pTarget)
{
	// Delete items in the other bags
	for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
	{
		if (const auto pBag = dynamic_cast<Bag*>(pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, i)))
		{
			for (uint32 j = 0; j < pBag->GetBagSize(); j++)
			{
				if (pBag->GetItemByPos(static_cast<uint8>(j)))
					pTarget->DestroyItem(i, static_cast<uint8>(j), true);
			}
		}
	}

	// Delete equipment and bags
	for (uint8 i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; i++)
		if (pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
			pTarget->DestroyItem(INVENTORY_SLOT_BAG_0, i, true);
}

void CombatBotBaseAI::LoadSpec(const Player* pTarget, const CombatBotRoles role)
{
	switch (pTarget->getClass())
	{
	case CLASS_WARRIOR:
	{
		if (role == ROLE_TANK)
		{
			Talents.emplace_back("Anticipation", 5);
			Talents.emplace_back("Toughness", 5);
			Talents.emplace_back("Shield Specialization", 5);
			Talents.emplace_back("Improved Shield Block", 1);
			Talents.emplace_back("Defiance", 3);
			Talents.emplace_back("Last Stand", 1);
			Talents.emplace_back("Improved Sunder Armor", 3);
			Talents.emplace_back("Improved Taunt", 1);
			Talents.emplace_back("Concussion Blow", 1);
			Talents.emplace_back("Shield Mastery", 3);
			Talents.emplace_back("One-Handed Weapon Specialization", 2);
			Talents.emplace_back("Shield Slam", 1);
			Talents.emplace_back("Improved Defensive Stance", 3);
			Talents.emplace_back("Focused Rage", 3);
			Talents.emplace_back("Vitality", 5);
			Talents.emplace_back("Devastate", 1);
			Talents.emplace_back("Deflection", 5);
			Talents.emplace_back("Tactical Mastery", 3);
			Talents.emplace_back("Cruelty", 5);
			Talents.emplace_back("One-Handed Weapon Specialization", 5);
			Talents.emplace_back("Iron Will", 2);
		}
		else
		{
			Talents.emplace_back("Cruelty", 5);
			Talents.emplace_back("Unbridled Wrath", 5);
			Talents.emplace_back("Commanding Presence", 5);
			Talents.emplace_back("Enrage", 5);
			Talents.emplace_back("Sweeping Strikes", 1);
			Talents.emplace_back("Weapon Mastery", 2);
			Talents.emplace_back("Dual Wield Specialization", 2);
			Talents.emplace_back("Flurry", 5);
			Talents.emplace_back("Bloodthirst", 1);
			Talents.emplace_back("Improved Whirlwind", 1);
			Talents.emplace_back("Dual Wield Specialization", 5);
			Talents.emplace_back("Improved Berserker Stance", 5);
			Talents.emplace_back("Improved Heroic Strike", 3);
			Talents.emplace_back("Deflection", 3);
			Talents.emplace_back("Iron Will", 5);
			Talents.emplace_back("Improved Thunder Clap", 3);
			Talents.emplace_back("Anger Management", 1);
			Talents.emplace_back("Deep Wounds", 3);
			Talents.emplace_back("Impale", 2);
			Talents.emplace_back("Death Wish", 1);
		}
		break;
	}
	case CLASS_HUNTER:
	{
		if (role == ROLE_RANGE_DPS)
		{
			Talents.emplace_back("Improved Aspect of the Hawk", 5);
			Talents.emplace_back("Focused Fire", 2);
			Talents.emplace_back("Improved Revive Pet", 2);
			Talents.emplace_back("Thick Hide", 1);
			Talents.emplace_back("Bestial Swiftness", 1);
			Talents.emplace_back("Unleashed Fury", 5);
			Talents.emplace_back("Ferocity", 5);
			Talents.emplace_back("Intimidation", 1);
			Talents.emplace_back("Bestial Discipline", 2);
			Talents.emplace_back("Improved Mend Pet", 1);
			Talents.emplace_back("Frenzy", 4);
			Talents.emplace_back("Improved Mend Pet", 2);
			Talents.emplace_back("Bestial Wrath", 1);
			Talents.emplace_back("Spirit Bond", 2);
			Talents.emplace_back("Catlike Reflexes", 2);
			Talents.emplace_back("Serpent's Swiftness", 5);
			Talents.emplace_back("The Beast Within", 1);
			Talents.emplace_back("Lethal Shots", 5);
			Talents.emplace_back("Efficiency", 5);
			Talents.emplace_back("Go for the Throat", 2);
			Talents.emplace_back("Rapid Killing", 2);
			Talents.emplace_back("Aimed Shot", 1);
			Talents.emplace_back("Mortal Shots", 5);

		}
		break;
	}
	case CLASS_WARLOCK:
	{
		if (role == ROLE_RANGE_DPS)
		{
			Talents.emplace_back("Improved Corruption", 5);
			Talents.emplace_back("Improved Drain Soul", 2);
			Talents.emplace_back("Improved Life Tap", 2);
			Talents.emplace_back("Soul Siphon", 2);
			Talents.emplace_back("Fel Concentration", 5);
			Talents.emplace_back("Nightfall", 2);
			Talents.emplace_back("Grim Reach", 2);
			Talents.emplace_back("Siphon Life", 1);
			Talents.emplace_back("Improved Curse of Agony", 1);
			Talents.emplace_back("Amplify Curse", 1);
			Talents.emplace_back("Shadow Embrace", 2);
			Talents.emplace_back("Shadow Mastery", 5);
			Talents.emplace_back("Dark Pact", 1);
			Talents.emplace_back("Improved Healthstone", 2);
			Talents.emplace_back("Demonic Embrace", 5);
			Talents.emplace_back("Fel Intellect", 3);
			Talents.emplace_back("Demonic Aegis", 3);
			Talents.emplace_back("Improved Succubus", 3);
			Talents.emplace_back("Unholy Power", 5);
			Talents.emplace_back("Fel Stamina", 3);
			Talents.emplace_back("Fel Domination", 1);
			Talents.emplace_back("Mana Feed", 3);
			Talents.emplace_back("Master Demonologist", 2);
		}
			break;
	}
	case CLASS_MAGE:
	{
		if (role == ROLE_RANGE_DPS)
		{
			Talents.emplace_back("Improved Frostbolt", 5);
			Talents.emplace_back("Elemental Precision", 3);
			Talents.emplace_back("Ice Shards", 5);
			Talents.emplace_back("Improved Frost Nova", 2);
			Talents.emplace_back("Piercing Ice", 3);
			Talents.emplace_back("Icy Veins", 1);
			Talents.emplace_back("Arctic Reach", 2);
			Talents.emplace_back("Frost Channeling", 3);
			Talents.emplace_back("Improved Cone of Cold", 3);
			Talents.emplace_back("Cold Snap", 1);
			Talents.emplace_back("Ice Barrier", 1);
			Talents.emplace_back("Frozen Core", 2);
			Talents.emplace_back("Winter's Chill", 5);
			Talents.emplace_back("Arctic Winds", 5);
			Talents.emplace_back("Empowered Frostbolt", 5);
			Talents.emplace_back("Arcane Subtlety", 2);
			Talents.emplace_back("Arcane Focus", 3);
			Talents.emplace_back("Magic Absorption", 5);
			Talents.emplace_back("Arcane Concentration", 5);
		}
			break;
	}
	case CLASS_ROGUE:
	{
		if (role == ROLE_MELEE_DPS)
		{
			Talents.emplace_back("Improved Sinister Strike", 2);
			Talents.emplace_back("Lightning Reflexes", 3);
			Talents.emplace_back("Precision", 5);
			Talents.emplace_back("Improved Slice and Dice", 3);
			Talents.emplace_back("Endurance", 2);
			Talents.emplace_back("Improved Sprint", 2);
			Talents.emplace_back("Dual Wield Specialization", 5);
			Talents.emplace_back("Blade Flurry", 1);
			Talents.emplace_back("Sword Specialization", 5);
			Talents.emplace_back("Weapon Expertise", 2);
			Talents.emplace_back("Aggression", 3);
			Talents.emplace_back("Adrenaline Rush", 1);
			Talents.emplace_back("Vitality", 2);
			Talents.emplace_back("Combat Potency", 5);
			Talents.emplace_back("Surprise Attacks", 1);
			Talents.emplace_back("Malice", 5);
			Talents.emplace_back("Improved Eviscerate", 1);
			Talents.emplace_back("Lethality", 5);
			Talents.emplace_back("Ruthlessness", 3);
			Talents.emplace_back("Murder", 2);
			Talents.emplace_back("Relentless Strikes", 1);
			Talents.emplace_back("Improved Expose Armor", 2);
		}
			break;
	}
	case CLASS_PRIEST:
	{
		if (role == ROLE_RANGE_DPS)
		{
			Talents.emplace_back("Spirit Tap", 5);
			Talents.emplace_back("Improved Shadow Word: Pain", 2);
			Talents.emplace_back("Shadow Affinity", 3);
			Talents.emplace_back("Shadow Focus", 5);
			Talents.emplace_back("Improved Mind Blast", 5);
			Talents.emplace_back("Mind Flay", 1);
			Talents.emplace_back("Shadow Reach", 2);
			Talents.emplace_back("Shadow Weaving", 4);
			Talents.emplace_back("Vampiric Embrace", 1);
			Talents.emplace_back("Focused Mind", 3);
			Talents.emplace_back("Darkness", 5);
			Talents.emplace_back("Shadowform", 1);
			Talents.emplace_back("Misery", 5);
			Talents.emplace_back("Vampiric Touch", 1);
			Talents.emplace_back("Shadow Power", 4);
			Talents.emplace_back("Unbreakable Will", 5);
			Talents.emplace_back("Improved Power Word: Shield", 3);
			Talents.emplace_back("Improved Power Word: Fortitude", 2);
			Talents.emplace_back("Meditation", 3);
			Talents.emplace_back("Inner Focus", 1);
		}
		else if( role == ROLE_HEALER)
		{
			Talents.emplace_back("Holy Specialization", 5);
			Talents.emplace_back("Divine Fury",5);
			Talents.emplace_back("Improved Renew", 3);
			Talents.emplace_back("Inspiration", 3);
			Talents.emplace_back("Improved Healing", 3);
			Talents.emplace_back("Holy Reach", 2);
			Talents.emplace_back("Spiritual Guidance", 5);
			Talents.emplace_back("Spiritual Healing", 5);
			Talents.emplace_back("Holy Concentration", 3);
			Talents.emplace_back("Healing Focus", 1);
			Talents.emplace_back("Empowered Healing", 5);
			Talents.emplace_back("Unbreakable Will", 5);
			Talents.emplace_back("Improved Power Word: Shield", 3);
			Talents.emplace_back("Improved Power Word: Fortitude", 2);
			Talents.emplace_back("Meditation", 3);
			Talents.emplace_back("Inner Focus", 1);
			Talents.emplace_back("Mental Agility", 5);
			Talents.emplace_back("Absolution", 1);
			Talents.emplace_back("Divine Spirit", 1);
		}
			break;
	}
	case CLASS_SHAMAN:
	{
		if (role == ROLE_RANGE_DPS)
		{
			Talents.emplace_back("Convection", 5);
			Talents.emplace_back("Concussion", 5);
			Talents.emplace_back("Call of Thunder", 5);
			Talents.emplace_back("Elemental Focus", 1);
			Talents.emplace_back("Reverberation", 5);
			Talents.emplace_back("Storm Reach", 2);
			Talents.emplace_back("Unrelenting Storm", 5);
			Talents.emplace_back("Lightning Mastery", 5);
			Talents.emplace_back("Lightning Mastery", 5);
			Talents.emplace_back("Elemental Fury", 1);
			Talents.emplace_back("Elemental Mastery", 1);
			Talents.emplace_back("Lightning Overload", 5);
			Talents.emplace_back("Totem of Wrath", 1);
			Talents.emplace_back("Call of Flame", 3);
			Talents.emplace_back("Eye of the Storm", 3);
			Talents.emplace_back("Elemental Precision", 3);
			Talents.emplace_back("Ancestral Knowledge", 5);
			Talents.emplace_back("Improved Lightning Shield", 3);
			Talents.emplace_back("Improved Ghost Wolf", 2);
			Talents.emplace_back("Shamanistic Focus", 1);
		}
		else if (role == ROLE_HEALER)
		{
			Talents.emplace_back("Tidal Focus", 5);
			Talents.emplace_back("Improved Healing Wave", 5);
			Talents.emplace_back("Healing Focus", 5);
			Talents.emplace_back("Tidal Mastery", 5);
			Talents.emplace_back("Healing Way", 3);
			Talents.emplace_back("Healing Grace", 3);
			Talents.emplace_back("Purification", 5);
			Talents.emplace_back("Nature's Swiftness", 1);
			Talents.emplace_back("Restorative Totems", 5);
			Talents.emplace_back("Improved Chain Heal", 2);
			Talents.emplace_back("Nature's Blessing", 3);
			Talents.emplace_back("Mana Tide Totem", 1);
			Talents.emplace_back("Earth Shield", 1);
			Talents.emplace_back("Ancestral Knowledge", 5);
			Talents.emplace_back("Focused Mind", 3);
			Talents.emplace_back("Totemic Mastery", 1);
			Talents.emplace_back("Totemic Focus", 5);
			Talents.emplace_back("Ancestral Healing", 3);
		}
		else if (role == ROLE_MELEE_DPS)
		{
			Talents.emplace_back("Ancestral Knowledge", 5);
			Talents.emplace_back("Thundering Strikes", 5);
			Talents.emplace_back("Improved Lightning Shield", 3);
			Talents.emplace_back("Shamanistic Focus", 1);
			Talents.emplace_back("Enhancing Totems", 1);
			Talents.emplace_back("Flurry", 5);
			Talents.emplace_back("Elemental Weapons", 3);
			Talents.emplace_back("Improved Weapon Totems", 2);
			Talents.emplace_back("Weapon Mastery", 5);
			Talents.emplace_back("Mental Quickness", 3);
			Talents.emplace_back("Stormstrike", 1);
			Talents.emplace_back("Enhancing Totems", 2);
			Talents.emplace_back("Unleashed Rage", 5);
			Talents.emplace_back("Shamanistic Rage", 1);
			Talents.emplace_back("Convection",5 );
			Talents.emplace_back("Elemental Warding", 3);
			Talents.emplace_back("Concussion", 2);
			Talents.emplace_back("Reverberation", 5);
			Talents.emplace_back("Elemental Devastation", 3);
			Talents.emplace_back("Eye of the Storm", 2);
		}
		else if (role == ROLE_TANK)
		{
			Talents.emplace_back("Ancestral Knowledge", 5);
			Talents.emplace_back("Shield Specialization", 5);
			Talents.emplace_back("Anticipation", 5);
			Talents.emplace_back("Toughness", 5);
			Talents.emplace_back("Thundering Strikes", 5);
			Talents.emplace_back("Improved Lightning Shield", 3);
			Talents.emplace_back("Flurry", 5);
			Talents.emplace_back("Spirit Weapons", 1);
			Talents.emplace_back("Weapon Mastery", 5);
			Talents.emplace_back("Unleashed Rage", 5);
			Talents.emplace_back("Shamanistic Rage", 1);
			Talents.emplace_back("Improved Weapon Totems", 2);
			Talents.emplace_back("Mental Quickness", 3);
			Talents.emplace_back("Guardian Totems", 2);
			Talents.emplace_back("Convection", 5);
			Talents.emplace_back("Elemental Warding", 3);
			Talents.emplace_back("Shamanistic Focus", 1);
		}
			break;
	}
	case CLASS_DRUID:
	{
		if (role == ROLE_RANGE_DPS)
		{
			Talents.emplace_back("Starlight Wrath", 5);
			Talents.emplace_back("Focused Starlight", 2);
			Talents.emplace_back("Improved Moonfire", 2);
			Talents.emplace_back("Insect Swarm", 1);
			Talents.emplace_back("Nature's Reach", 2);
			Talents.emplace_back("Brambles", 3);
			Talents.emplace_back("Vengeance", 5);
			Talents.emplace_back("Moonglow", 3);
			Talents.emplace_back("Lunar Guidance", 3);
			Talents.emplace_back("Nature's Grace", 1);
			Talents.emplace_back("Moonfury", 2);
			Talents.emplace_back("Moonkin Form", 1);
			Talents.emplace_back("Balance of Power", 2);
			Talents.emplace_back("Moonfury", 5);
			Talents.emplace_back("Dreamstate", 3);
			Talents.emplace_back("Wrath of Cenarius", 5);
			Talents.emplace_back("Force of Nature", 1);
			Talents.emplace_back("Celestial Focus", 3);
			Talents.emplace_back("Control of Nature", 3);
			Talents.emplace_back("Improved Mark of the Wild", 5);
			Talents.emplace_back("Natural Shapeshifter", 3);
			Talents.emplace_back("Naturalist", 2);
			Talents.emplace_back("Intensity", 1);
		}
		else if (role == ROLE_HEALER)
		{
			Talents.emplace_back("Improved Mark of the Wild", 5);
			Talents.emplace_back("Nature's Focus", 5);
			Talents.emplace_back("Naturalist", 5);
			Talents.emplace_back("Tranquil Spirit", 5);
			Talents.emplace_back("Gift of Nature",5);
			Talents.emplace_back("Intensity", 3);
			Talents.emplace_back("Nature's Swiftness", 1);
			Talents.emplace_back("Empowered Touch", 1);
			Talents.emplace_back("Swiftmend", 1);
			Talents.emplace_back("Empowered Touch", 2);
			Talents.emplace_back("Living Spirit", 3);
			Talents.emplace_back("Empowered Rejuvenation", 5);
			Talents.emplace_back("Tree of Life", 1);
			Talents.emplace_back("Subtlety", 5);
			Talents.emplace_back("Natural Shapeshifter", 3);
			Talents.emplace_back("Improved Rejuvenation", 3);
			Talents.emplace_back("Improved Regrowth", 5);
			Talents.emplace_back("Natural Perfection", 3);
			Talents.emplace_back("Improved Tranquility", 1);
		}
		else if (role == ROLE_MELEE_DPS)
		{
			Talents.emplace_back("Ferocity", 5);
			Talents.emplace_back("Feral Instinct", 3);
			Talents.emplace_back("Feral Aggression", 2);
			Talents.emplace_back("Sharpened Claws", 3);
			Talents.emplace_back("Feral Swiftness", 2);
			Talents.emplace_back("Predatory Strikes", 3);
			Talents.emplace_back("Shredding Attacks", 2);
			Talents.emplace_back("Savage Fury", 2);
			Talents.emplace_back("Faerie Fire (Feral)", 1);
			Talents.emplace_back("Thick Hide", 2);
			Talents.emplace_back("Heart of the Wild", 5);
			Talents.emplace_back("Leader of the Pack", 1);
			Talents.emplace_back("Improved Leader of the Pack", 2);
			Talents.emplace_back("Survival of the Fittest", 2);
			Talents.emplace_back("Predatory Instincts", 5);
			Talents.emplace_back("Mangle", 1);
			Talents.emplace_back("Improved Mark of the Wild", 5);
			Talents.emplace_back("Furor", 5);
			Talents.emplace_back("Natural Shapeshifter", 3);
			Talents.emplace_back("Omen of Clarity", 1);
			Talents.emplace_back("Primal Fury", 2);
			Talents.emplace_back("Primal Tenacity", 3);
			Talents.emplace_back("Feral Aggression", 3);
		}
		else if (role == ROLE_TANK)
		{
			Talents.emplace_back("Ferocity", 5);
			Talents.emplace_back("Feral Instinct", 3);
			Talents.emplace_back("Thick Hide", 3);
			Talents.emplace_back("Feral Aggression", 5);
			Talents.emplace_back("Feral Charge", 1);
			Talents.emplace_back("Predatory Strikes", 3);
			Talents.emplace_back("Sharpened Claws", 3);
			Talents.emplace_back("Faerie Fire (Feral)", 1);
			Talents.emplace_back("Primal Fury", 2);
			Talents.emplace_back("Heart of the Wild", 5);
			Talents.emplace_back("Leader of the Pack", 1);
			Talents.emplace_back("Improved Leader of the Pack", 2);
			Talents.emplace_back("Survival of the Fittest", 3);
			Talents.emplace_back("Predatory Instincts", 4);
			Talents.emplace_back("Mangle", 1);
			Talents.emplace_back("Primal Tenacity", 3);
			Talents.emplace_back("Predatory Instincts", 5);
			Talents.emplace_back("Furor", 5);
			Talents.emplace_back("Natural Shapeshifter", 3);
			Talents.emplace_back("Improved Mark of the Wild", 5);
			Talents.emplace_back("Omen of Clarity", 1);
			Talents.emplace_back("Brutal Impact", 1);
		}
			break;
	}
	case CLASS_PALADIN:
	{
		if (role == ROLE_HEALER)
		{
			Talents.emplace_back("Divine Intellect", 5);
			Talents.emplace_back("Spiritual Focus", 5);
			Talents.emplace_back("Healing Light", 3);
			Talents.emplace_back("Improved Lay on Hands", 2);
			Talents.emplace_back("Illumination", 5);
			Talents.emplace_back("Divine Favor", 1);
			Talents.emplace_back("Sanctified Light", 3);
			Talents.emplace_back("Improved Blessing of Wisdom", 2);
			Talents.emplace_back("Holy Power", 5);
			Talents.emplace_back("Aura Mastery", 1);
			Talents.emplace_back("Purifying Power", 2);
			Talents.emplace_back("Light's Grace", 1);
			Talents.emplace_back("Holy Guidance", 5);
			Talents.emplace_back("Divine Illumination", 1);
			Talents.emplace_back("Light's Grace", 3);
			Talents.emplace_back("Holy Shock", 1);
			Talents.emplace_back("Unyielding Faith", 2);
			Talents.emplace_back("Improved Devotion Aura", 5);
			Talents.emplace_back("Toughness", 5);
			Talents.emplace_back("Blessing of Kings", 1);
			Talents.emplace_back("Blessed Life", 3);
			Talents.emplace_back("Pure of Heart", 1);
		}
		else if (role == ROLE_MELEE_DPS)
		{
			Talents.emplace_back("Improved Blessing of Might", 5);
			Talents.emplace_back("Improved Judgement", 2);
			Talents.emplace_back("Benediction", 3);
			Talents.emplace_back("Seal of Command", 1);
			Talents.emplace_back("Conviction", 5);
			Talents.emplace_back("Benediction", 5);
			Talents.emplace_back("Eye for an Eye", 2);
			Talents.emplace_back("Two-Handed Weapon Specialization", 3);
			Talents.emplace_back("Crusade", 2);
			Talents.emplace_back("Vengeance", 5);
			Talents.emplace_back("Repentance", 1);
			Talents.emplace_back("Sanctified Seals", 3);
			Talents.emplace_back("Sanctity Aura", 1);
			Talents.emplace_back("Fanaticism", 5);
			Talents.emplace_back("Crusader Strike", 1);
			Talents.emplace_back("Divine Strength", 5);
			Talents.emplace_back("Improved Devotion Aura", 5);
			Talents.emplace_back("Precision", 3);
			Talents.emplace_back("Toughness", 2);
			Talents.emplace_back("Blessing of Kings", 1);
			Talents.emplace_back("Toughness", 5);
			Talents.emplace_back("Guardian's Favor", 1);
		}
		else if (role == ROLE_TANK)
		{
			Talents.emplace_back("Redoubt", 5);
			Talents.emplace_back("Toughness", 5);
			Talents.emplace_back("Blessing of Kings", 1);
			Talents.emplace_back("Improved Devotion Aura", 5);
			Talents.emplace_back("Improved Righteous Fury", 3);
			Talents.emplace_back("Precision", 3);
			Talents.emplace_back("Shield Specialization", 3);
			Talents.emplace_back("Anticipation", 5);
			Talents.emplace_back("Blessing of Sanctuary", 1);
			Talents.emplace_back("Holy Shield", 1);
			Talents.emplace_back("Reckoning", 5);
			Talents.emplace_back("Sacred Duty", 2);
			Talents.emplace_back("One-Handed Weapon Specialization", 1);
			Talents.emplace_back("Avenger's Shield", 1);
			Talents.emplace_back("One-Handed Weapon Specialization", 5);
			Talents.emplace_back("Divine Strength", 5);
			Talents.emplace_back("Combat Expertise", 5);
			Talents.emplace_back("Spell Warding", 2);
			Talents.emplace_back("Improved Holy Shield", 2);
			Talents.emplace_back("Stoicism", 2);
		}
			break;
	}
	}
}

uint32 CombatBotBaseAI::GetTalentID(const Player* pTarget, const std::string& TalentName)
{
	auto Talent = sPlayerBotMgr.GetTalentMap();
	const auto Class = static_cast<Classes>(pTarget->getClass());
	for (const auto& [Name, ID] : Talent[Class])
		if (Name == TalentName)
			return ID;

	return 0;
}

void CombatBotBaseAI::LearnTalents(Player* pTarget, const CombatBotRoles Role)
{
	if (pTarget->GetLevel() < 10) return;
	LoadSpec(pTarget, Role);
	for (const auto& [Name, Rank] : Talents)
	{
		if (!pTarget->GetFreeTalentPoints())
			return;

		const uint32 TalentID = GetTalentID(pTarget, Name);
		uint32 checkRank = 0;
		while (checkRank < Rank)
		{
			pTarget->LearnTalent(TalentID, checkRank);
			checkRank++;
		}
	}
}

void CombatBotBaseAI::LearnSpellsAndSkills(Player* pTarget, const uint32 SkillsTrainer)
{
	uint32 trainerID = 0;
	if (SkillsTrainer)
		trainerID = SkillsTrainer;
	else
	{
		const auto SpellTrainers = sPlayerBotMgr.GetSpellTrainers();
		for (const auto& [Class, ID] : SpellTrainers)
		{
			if (Class == pTarget->getClass())
			{
				trainerID = ID;
				break;
			}
		}
	}

	// Learn Spells
	if (const CreatureInfo* Trainer = sObjectMgr.GetCreatureTemplate(trainerID))
	{
		if (Trainer->TrainerTemplateId > 0)
		{
			TrainerSpellData const* trainerSpells = sObjectMgr.GetNpcTrainerTemplateSpells(Trainer->TrainerTemplateId);
			while (true)
			{
				bool hasNew = false;
				for (const auto& [ID, Spell] : trainerSpells->spellList)
				{
					TrainerSpell const* pSpell = &Spell;

					if (pTarget->HasSpell(pSpell->spell))
						continue;

					if (pSpell->reqSkill && !pTarget->HasSpell(pSpell->reqSkill))
						continue;

					const TrainerSpellState state = pTarget->GetTrainerSpellState(pSpell, pSpell->reqLevel);
					if (state == TRAINER_SPELL_GREEN)
					{
						pTarget->learnSpell(pSpell->spell, true);
						hasNew = true;
					}
				}
				if (!hasNew)
					break;
			}
		}
	}

	// Learn Skills
	if (!SkillsTrainer)
		LearnSpellsAndSkills(pTarget, 100000);
	if (SkillsTrainer)
		pTarget->UpdateSkillsForLevel(true);
}

bool CombatBotBaseAI::EquipNewItem(Player* pTarget, const uint32 pItemEntry, const uint8 pEquipSlot)
{
	if (!pTarget)
		return false;

	uint16 eDest;
	const InventoryResult tryEquipResult = pTarget->CanEquipNewItem(NULL_SLOT, eDest, pItemEntry, false);
	if (tryEquipResult == EQUIP_ERR_OK)
	{
		ItemPosCountVec sDest;
		const InventoryResult storeResult = pTarget->CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, sDest, pItemEntry, 1);
		if (storeResult == EQUIP_ERR_OK)
		{
			if (Item* pItem = pTarget->StoreNewItem(sDest, pItemEntry, true, Item::GenerateItemRandomPropertyId(pItemEntry)))
			{
				const InventoryResult equipResult = pTarget->CanEquipItem(NULL_SLOT, eDest, pItem, false);
				if (equipResult == EQUIP_ERR_OK)
				{
					pTarget->RemoveItem(pItem->GetBagSlot(), pItem->GetSlot(), true);
					pTarget->EquipItem(pEquipSlot, pItem, true);
					return true;
				}

				pItem->DestroyForPlayer(pTarget);
			}
		}
	}

	return false;
}

void CombatBotBaseAI::Enchant(Player* pTarget, Item* pItem, const uint32 EnchantID)
{
	if (!pTarget || !pItem || !EnchantID)
		return;

	pItem->ClearEnchantment(PERM_ENCHANTMENT_SLOT);
	pItem->SetEnchantment(PERM_ENCHANTMENT_SLOT, EnchantID, 0, 0);
	pTarget->ApplyEnchantment(pItem, true);
}

void CombatBotBaseAI::InitializeSets(Player* pTarget, Player* pLeader, uint8 role)
{
	if (pTarget->GetLevel() < 60)
		return;

	switch (pTarget->getClass())
	{
	case CLASS_WARRIOR:
	{
		switch (role)
		{
		case ROLE_TANK:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=23577/the-hungering-cold
			EquipNewItem(pTarget, 23577, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=20034/enchant-weapon-crusader
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 1900);

			//https://classic.wowhead.com/item=23043/the-face-of-death
			EquipNewItem(pTarget, 23043, EQUIPMENT_SLOT_OFFHAND);
			//https://classic.wowhead.com/spell=20017/enchant-shield-greater-stamina
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
			Enchant(pTarget, item, 929);

			//https://classic.wowhead.com/item=19368/dragonbreath-hand-cannon
			EquipNewItem(pTarget, 19368, EQUIPMENT_SLOT_RANGED);
			//https://classic.wowhead.com/item=10548/sniper-scope
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
			Enchant(pTarget, item, 664);

			//https://classic.wowhead.com/item=22418/dreadnaught-helmet
			EquipNewItem(pTarget, 22418, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/spell=24149/presence-of-might
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2583);

			//https://classic.wowhead.com/item=22732/mark-of-cthun
			EquipNewItem(pTarget, 22732, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=22419/dreadnaught-pauldrons
			EquipNewItem(pTarget, 22419, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/spell=29480/fortitude-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2716);

			//https://classic.wowhead.com/item=22938/cryptfiend-silk-cloak
			EquipNewItem(pTarget, 22938, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25086/enchant-cloak-dodge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2622);

			//https://classic.wowhead.com/item=22416/dreadnaught-breastplate
			EquipNewItem(pTarget, 22416, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20026/enchant-chest-major-health
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1892);

			//https://classic.wowhead.com/item=22423/dreadnaught-bracers
			EquipNewItem(pTarget, 22423, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=20011/enchant-bracer-superior-stamina
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 1886);

			//https://classic.wowhead.com/item=22421/dreadnaught-gauntlets
			EquipNewItem(pTarget, 22421, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25072/enchant-gloves-threat
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2613);

			//https://classic.wowhead.com/item=22422/dreadnaught-waistguard
			EquipNewItem(pTarget, 22422, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=22417/dreadnaught-legplates
			EquipNewItem(pTarget, 22417, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/spell=24149/presence-of-might
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2583);

			//https://classic.wowhead.com/item=22420/dreadnaught-sabatons
			EquipNewItem(pTarget, 22420, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=23059/ring-of-the-dreadnaught
			EquipNewItem(pTarget, 23059, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=21601/ring-of-emperor-veklor
			EquipNewItem(pTarget, 21601, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=19431/styleens-impeding-scarab
			EquipNewItem(pTarget, 19431, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=22954/kiss-of-the-spider
			EquipNewItem(pTarget, 22954, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		default:
		{
			Item* item = nullptr;

			if (pTarget->GetTeam() == ALLIANCE)
			{
				//https://classic.wowhead.com/item=23054/gressil-dawn-of-ruin
				EquipNewItem(pTarget, 23054, EQUIPMENT_SLOT_MAINHAND);
				//https://classic.wowhead.com/spell=20034/enchant-weapon-crusader
				item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
				Enchant(pTarget, item, 1900);

				//https://classic.wowhead.com/item=23577/the-hungering-cold
				EquipNewItem(pTarget, 23577, EQUIPMENT_SLOT_OFFHAND);
				//https://classic.wowhead.com/spell=20034/enchant-weapon-crusader
				item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
				Enchant(pTarget, item, 1900);
			}
			else
			{
				//https://classic.wowhead.com/item=22816/hatchet-of-sundered-bone
				EquipNewItem(pTarget, 22816, EQUIPMENT_SLOT_MAINHAND);
				//https://classic.wowhead.com/spell=20034/enchant-weapon-crusader
				item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
				Enchant(pTarget, item, 1900);

				//https://classic.wowhead.com/item=19363/crulshorukh-edge-of-chaos
				EquipNewItem(pTarget, 19363, EQUIPMENT_SLOT_OFFHAND);
				//https://classic.wowhead.com/spell=20034/enchant-weapon-crusader
				item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
				Enchant(pTarget, item, 1900);
			}

			//https://classic.wowhead.com/item=22812/nerubian-slavemaker
			EquipNewItem(pTarget, 22812, EQUIPMENT_SLOT_RANGED);
			//https://classic.wowhead.com/item=10548/sniper-scope
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
			Enchant(pTarget, item, 664);

			//https://classic.wowhead.com/item=12640/lionheart-helm
			EquipNewItem(pTarget, 12640, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/spell=15397/lesser-arcane-amalgamation
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 1506);

			//https://classic.wowhead.com/item=23053/stormrages-talisman-of-seething
			EquipNewItem(pTarget, 23053, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=21330/conquerors-spaulders
			EquipNewItem(pTarget, 21330, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/spell=29483/might-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2717);

			//https://classic.wowhead.com/item=23045/shroud-of-dominion
			EquipNewItem(pTarget, 23045, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2621);

			//https://classic.wowhead.com/item=23000/plated-abomination-ribcage
			EquipNewItem(pTarget, 23000, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1891);

			//https://classic.wowhead.com/item=22936/wristguards-of-vengeance
			EquipNewItem(pTarget, 22936, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=20010/enchant-bracer-superior-strength
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 1885);

			//https://classic.wowhead.com/item=21581/gauntlets-of-annihilation
			EquipNewItem(pTarget, 21581, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25080/enchant-gloves-superior-agility
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2564);

			//https://classic.wowhead.com/item=23219/girdle-of-the-mentor
			EquipNewItem(pTarget, 23219, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=22385/titanic-leggings
			EquipNewItem(pTarget, 22385, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/spell=15397/lesser-arcane-amalgamation
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 1506);

			//https://classic.wowhead.com/item=19387/chromatic-boots
			EquipNewItem(pTarget, 19387, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=23038/band-of-unnatural-forces
			EquipNewItem(pTarget, 23038, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=19384/master-dragonslayers-ring
			EquipNewItem(pTarget, 19384, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=22954/kiss-of-the-spider
			EquipNewItem(pTarget, 22954, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=23041/slayers-crest
			EquipNewItem(pTarget, 23041, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		}

		break;
	}
	case CLASS_PALADIN:
	{
		switch (role)
		{
		case ROLE_TANK:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=2243/hand-of-edward-the-odd
			EquipNewItem(pTarget, 2243, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=22749/enchant-weapon-spell-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2504);

			//https://classic.wowhead.com/item=22819/shield-of-condemnation
			EquipNewItem(pTarget, 22819, EQUIPMENT_SLOT_OFFHAND);
			//https://classic.wowhead.com/spell=20017/enchant-shield-greater-stamina
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
			Enchant(pTarget, item, 929);

			//https://classic.wowhead.com/item=22401/libram-of-hope
			EquipNewItem(pTarget, 22401, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=22428/redemption-headpiece
			EquipNewItem(pTarget, 22428, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/spell=24160/syncretists-sigil
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2584);

			//https://classic.wowhead.com/item=18814/choker-of-the-fire-lord
			EquipNewItem(pTarget, 18814, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=22429/redemption-spaulders
			EquipNewItem(pTarget, 22429, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/spell=29467/power-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2721);

			//https://classic.wowhead.com/item=22731/cloak-of-the-devoured
			EquipNewItem(pTarget, 22731, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25086/enchant-cloak-dodge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2622);

			//https://classic.wowhead.com/item=22425/redemption-tunic
			EquipNewItem(pTarget, 22425, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20026/enchant-chest-major-health
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1892);

			//https://classic.wowhead.com/item=22424/redemption-wristguards
			EquipNewItem(pTarget, 22424, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=23802/enchant-bracer-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 2566);

			//https://classic.wowhead.com/item=22426/redemption-handguards
			EquipNewItem(pTarget, 22426, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25079/enchant-gloves-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2617);

			//https://classic.wowhead.com/item=22431/redemption-girdle
			EquipNewItem(pTarget, 22431, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=22427/redemption-legguards
			EquipNewItem(pTarget, 22427, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/spell=24160/syncretists-sigil
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2584);

			//https://classic.wowhead.com/item=22430/redemption-boots
			EquipNewItem(pTarget, 22430, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=21709/ring-of-the-fallen-god
			EquipNewItem(pTarget, 21709, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=21210/signet-ring-of-the-bronze-dragonflight
			EquipNewItem(pTarget, 21210, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=23046/the-restrained-essence-of-sapphiron
			EquipNewItem(pTarget, 23046, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=19343/scrolls-of-blinding-light
			EquipNewItem(pTarget, 19343, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		case ROLE_HEALER:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=23056/hammer-of-the-twisting-nether
			EquipNewItem(pTarget, 23056, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=22750/enchant-weapon-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2505);

			//https://classic.wowhead.com/item=22819/shield-of-condemnation
			EquipNewItem(pTarget, 22819, EQUIPMENT_SLOT_OFFHAND);
			//https://classic.wowhead.com/spell=20017/enchant-shield-greater-stamina
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
			Enchant(pTarget, item, 929);

			//https://classic.wowhead.com/item=23006/libram-of-light
			EquipNewItem(pTarget, 23006, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=22428/redemption-headpiece
			EquipNewItem(pTarget, 22428, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/spell=24160/syncretists-sigil
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2584);

			//https://classic.wowhead.com/item=21712/amulet-of-the-fallen-god
			EquipNewItem(pTarget, 21712, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=22429/redemption-spaulders
			EquipNewItem(pTarget, 22429, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/spell=29475/resilience-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2715);

			//https://classic.wowhead.com/item=22960/cloak-of-suturing
			EquipNewItem(pTarget, 22960, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2621);

			//https://classic.wowhead.com/item=22425/redemption-tunic
			EquipNewItem(pTarget, 22425, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20028/enchant-chest-major-mana
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1893);

			//https://classic.wowhead.com/item=22424/redemption-wristguards
			EquipNewItem(pTarget, 22424, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=23802/enchant-bracer-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 2566);

			//https://classic.wowhead.com/item=22426/redemption-handguards
			EquipNewItem(pTarget, 22426, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25079/enchant-gloves-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2617);

			//https://classic.wowhead.com/item=22431/redemption-girdle
			EquipNewItem(pTarget, 22431, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=22427/redemption-legguards
			EquipNewItem(pTarget, 22427, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/spell=24160/syncretists-sigil
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2584);

			//https://classic.wowhead.com/item=22430/redemption-boots
			EquipNewItem(pTarget, 22430, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=23066/ring-of-redemption
			EquipNewItem(pTarget, 23066, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=21620/ring-of-the-martyr
			EquipNewItem(pTarget, 21620, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=19395/rejuvenating-gem
			EquipNewItem(pTarget, 19395, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=23047/eye-of-the-dead
			EquipNewItem(pTarget, 23047, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		default:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=2243/hand-of-edward-the-odd
			EquipNewItem(pTarget, 2243, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=22749/enchant-weapon-spell-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2504);

			//https://classic.wowhead.com/item=23049/sapphirons-left-eye
			EquipNewItem(pTarget, 23049, EQUIPMENT_SLOT_OFFHAND);

			//https://classic.wowhead.com/item=22401/libram-of-hope
			EquipNewItem(pTarget, 22401, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=21387/avengers-crown
			EquipNewItem(pTarget, 21387, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/item=18330/arcanum-of-focus
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2544);

			//https://classic.wowhead.com/item=18814/choker-of-the-fire-lord
			EquipNewItem(pTarget, 18814, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=21391/avengers-pauldrons
			EquipNewItem(pTarget, 21391, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/item=23545/power-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2721);

			//https://classic.wowhead.com/item=22731/cloak-of-the-devoured
			EquipNewItem(pTarget, 22731, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2621);

			//https://classic.wowhead.com/item=21389/avengers-breastplate
			EquipNewItem(pTarget, 21389, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1891);

			//https://classic.wowhead.com/item=21611/burrower-bracers
			EquipNewItem(pTarget, 21611, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=23802/enchant-bracer-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 2566);

			//https://classic.wowhead.com/item=21623/gauntlets-of-the-righteous-champion
			EquipNewItem(pTarget, 21623, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25080/enchant-gloves-superior-agility
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2564);

			//https://classic.wowhead.com/item=22730/eyestalk-waist-cord
			EquipNewItem(pTarget, 22730, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=21390/avengers-legguards
			EquipNewItem(pTarget, 21390, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/item=18330/arcanum-of-focus
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2544);

			//https://classic.wowhead.com/item=21388/avengers-greaves
			EquipNewItem(pTarget, 21388, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=21709/ring-of-the-fallen-god
			EquipNewItem(pTarget, 21709, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=23031/band-of-the-inevitable
			EquipNewItem(pTarget, 23031, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=19379/neltharions-tear
			EquipNewItem(pTarget, 19379, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=23046/the-restrained-essence-of-sapphiron
			EquipNewItem(pTarget, 23046, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		}

		break;
	}
	case CLASS_HUNTER:
	{
		Item* item = nullptr;

		//https://classic.wowhead.com/item=22816/hatchet-of-sundered-bone
		EquipNewItem(pTarget, 22816, EQUIPMENT_SLOT_MAINHAND);
		//https://classic.wowhead.com/spell=23800/enchant-weapon-agility
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
		Enchant(pTarget, item, 2564);

		//https://classic.wowhead.com/item=22802/kingsfall
		EquipNewItem(pTarget, 22802, EQUIPMENT_SLOT_OFFHAND);
		//https://classic.wowhead.com/spell=23800/enchant-weapon-agility
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
		Enchant(pTarget, item, 2564);

		//https://classic.wowhead.com/item=22812/nerubian-slavemaker
		EquipNewItem(pTarget, 22812, EQUIPMENT_SLOT_RANGED);
		//https://classic.wowhead.com/item=18283/biznicks-247x128-accurascope
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
		Enchant(pTarget, item, 2523);

		//https://classic.wowhead.com/item=22438/cryptstalker-headpiece
		EquipNewItem(pTarget, 22438, EQUIPMENT_SLOT_HEAD);
		//https://classic.wowhead.com/item=11647/lesser-arcanum-of-voracity
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
		Enchant(pTarget, item, 1508);

		//https://classic.wowhead.com/item=19377/prestors-talisman-of-connivery
		EquipNewItem(pTarget, 19377, EQUIPMENT_SLOT_NECK);

		//https://classic.wowhead.com/item=22439/cryptstalker-spaulders
		EquipNewItem(pTarget, 22439, EQUIPMENT_SLOT_SHOULDERS);
		//https://classic.wowhead.com/spell=29483/might-of-the-scourge
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
		Enchant(pTarget, item, 2717);

		//https://classic.wowhead.com/item=21710/cloak-of-the-fallen-god
		EquipNewItem(pTarget, 21710, EQUIPMENT_SLOT_BACK);
		//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
		Enchant(pTarget, item, 2621);

		//https://classic.wowhead.com/item=22436/cryptstalker-tunic
		EquipNewItem(pTarget, 22436, EQUIPMENT_SLOT_CHEST);
		//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
		Enchant(pTarget, item, 1891);

		//https://classic.wowhead.com/item=22443/cryptstalker-wristguards
		EquipNewItem(pTarget, 22443, EQUIPMENT_SLOT_WRISTS);
		//https://classic.wowhead.com/spell=20008/enchant-bracer-greater-intellect
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
		Enchant(pTarget, item, 1883);

		//https://classic.wowhead.com/item=22441/cryptstalker-handguards
		EquipNewItem(pTarget, 22441, EQUIPMENT_SLOT_HANDS);
		//https://classic.wowhead.com/spell=25080/enchant-gloves-superior-agility
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
		Enchant(pTarget, item, 2564);

		//https://classic.wowhead.com/item=22442/cryptstalker-girdle
		EquipNewItem(pTarget, 22442, EQUIPMENT_SLOT_WAIST);

		//https://classic.wowhead.com/item=22437/cryptstalker-legguards
		EquipNewItem(pTarget, 22437, EQUIPMENT_SLOT_LEGS);
		//https://classic.wowhead.com/item=11647/lesser-arcanum-of-voracity
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
		Enchant(pTarget, item, 1508);

		//https://classic.wowhead.com/item=22440/cryptstalker-boots
		EquipNewItem(pTarget, 22440, EQUIPMENT_SLOT_FEET);
		//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
		Enchant(pTarget, item, 911);

		//https://classic.wowhead.com/item=23067/ring-of-the-cryptstalker
		EquipNewItem(pTarget, 23067, EQUIPMENT_SLOT_FINGER1);

		//https://classic.wowhead.com/item=22961/band-of-reanimation
		EquipNewItem(pTarget, 22961, EQUIPMENT_SLOT_FINGER2);

		//https://classic.wowhead.com/item=23041/slayers-crest
		EquipNewItem(pTarget, 23041, EQUIPMENT_SLOT_TRINKET1);

		//https://classic.wowhead.com/item=23570/jom-gabbar
		EquipNewItem(pTarget, 23570, EQUIPMENT_SLOT_TRINKET2);

		break;
	}
	case CLASS_ROGUE:
	{
		Item* item = nullptr;

		//https://classic.wowhead.com/item=23054/gressil-dawn-of-ruin
		EquipNewItem(pTarget, 23054, EQUIPMENT_SLOT_MAINHAND);
		//https://classic.wowhead.com/spell=20034/enchant-weapon-crusader
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
		Enchant(pTarget, item, 1900);

		//https://classic.wowhead.com/item=23577/the-hungering-cold
		EquipNewItem(pTarget, 23577, EQUIPMENT_SLOT_OFFHAND);
		//https://classic.wowhead.com/spell=20034/enchant-weapon-crusader
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
		Enchant(pTarget, item, 1900);

		//https://classic.wowhead.com/item=22812/nerubian-slavemaker
		EquipNewItem(pTarget, 22812, EQUIPMENT_SLOT_RANGED);
		//https://classic.wowhead.com/item=10548/sniper-scope
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
		Enchant(pTarget, item, 664);

		//https://classic.wowhead.com/item=22478/bonescythe-helmet
		EquipNewItem(pTarget, 22478, EQUIPMENT_SLOT_HEAD);
		//https://classic.wowhead.com/spell=24161/deaths-embrace
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
		Enchant(pTarget, item, 2585);

		//https://classic.wowhead.com/item=19377/prestors-talisman-of-connivery
		EquipNewItem(pTarget, 19377, EQUIPMENT_SLOT_NECK);

		//https://classic.wowhead.com/item=22479/bonescythe-pauldrons
		EquipNewItem(pTarget, 22479, EQUIPMENT_SLOT_SHOULDERS);
		//https://classic.wowhead.com/spell=29483/might-of-the-scourge
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
		Enchant(pTarget, item, 2717);

		//https://classic.wowhead.com/item=23045/shroud-of-dominion
		EquipNewItem(pTarget, 23045, EQUIPMENT_SLOT_BACK);
		//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
		Enchant(pTarget, item, 2621);

		//https://classic.wowhead.com/item=22476/bonescythe-breastplate
		EquipNewItem(pTarget, 22476, EQUIPMENT_SLOT_CHEST);
		//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
		Enchant(pTarget, item, 1891);

		//https://classic.wowhead.com/item=22483/bonescythe-bracers
		EquipNewItem(pTarget, 22483, EQUIPMENT_SLOT_WRISTS);
		//https://classic.wowhead.com/spell=20010/enchant-bracer-superior-strength
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
		Enchant(pTarget, item, 1885);

		//https://classic.wowhead.com/item=22481/bonescythe-gauntlets
		EquipNewItem(pTarget, 22481, EQUIPMENT_SLOT_HANDS);
		//https://classic.wowhead.com/spell=25080/enchant-gloves-superior-agility
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
		Enchant(pTarget, item, 2564);

		//https://classic.wowhead.com/item=21586/belt-of-never-ending-agony
		EquipNewItem(pTarget, 21586, EQUIPMENT_SLOT_WAIST);

		//https://classic.wowhead.com/item=22477/bonescythe-legplates
		EquipNewItem(pTarget, 22477, EQUIPMENT_SLOT_LEGS);
		//https://classic.wowhead.com/spell=24161/deaths-embrace
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
		Enchant(pTarget, item, 2585);

		//https://classic.wowhead.com/item=22480/bonescythe-sabatons
		EquipNewItem(pTarget, 22480, EQUIPMENT_SLOT_FEET);
		//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
		Enchant(pTarget, item, 911);

		//https://classic.wowhead.com/item=23060/bonescythe-ring
		EquipNewItem(pTarget, 23060, EQUIPMENT_SLOT_FINGER1);

		//https://classic.wowhead.com/item=23038/band-of-unnatural-forces
		EquipNewItem(pTarget, 23038, EQUIPMENT_SLOT_FINGER2);

		//https://classic.wowhead.com/item=22954/kiss-of-the-spider
		EquipNewItem(pTarget, 22954, EQUIPMENT_SLOT_TRINKET1);

		//https://classic.wowhead.com/item=23041/slayers-crest
		EquipNewItem(pTarget, 23041, EQUIPMENT_SLOT_TRINKET2);

		break;
	}
	case CLASS_PRIEST:
	{
		switch (role)
		{
		case ROLE_HEALER:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=23056/hammer-of-the-twisting-nether
			EquipNewItem(pTarget, 23056, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=22750/enchant-weapon-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2505);

			//https://classic.wowhead.com/item=23048/sapphirons-right-eye
			EquipNewItem(pTarget, 23048, EQUIPMENT_SLOT_OFFHAND);

			//https://classic.wowhead.com/item=23009/wand-of-the-whispering-dead
			EquipNewItem(pTarget, 23009, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=22514/circlet-of-faith
			EquipNewItem(pTarget, 22514, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/spell=24167/prophetic-aura
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2590);

			//https://classic.wowhead.com/item=21712/amulet-of-the-fallen-god
			EquipNewItem(pTarget, 21712, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=22515/shoulderpads-of-faith
			EquipNewItem(pTarget, 22515, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/spell=29475/resilience-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2715);

			//https://classic.wowhead.com/item=22960/cloak-of-suturing
			EquipNewItem(pTarget, 22960, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2621);

			//https://classic.wowhead.com/item=22512/robe-of-faith
			EquipNewItem(pTarget, 22512, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1891);

			//https://classic.wowhead.com/item=21604/bracelets-of-royal-redemption
			EquipNewItem(pTarget, 21604, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=23802/enchant-bracer-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 2566);

			//https://classic.wowhead.com/item=22517/gloves-of-faith
			EquipNewItem(pTarget, 22517, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25079/enchant-gloves-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2617);

			//https://classic.wowhead.com/item=22518/belt-of-faith
			EquipNewItem(pTarget, 22518, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=22513/leggings-of-faith
			EquipNewItem(pTarget, 22513, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/spell=24167/prophetic-aura
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2590);

			//https://classic.wowhead.com/item=22516/sandals-of-faith
			EquipNewItem(pTarget, 22516, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=23061/ring-of-faith
			EquipNewItem(pTarget, 23061, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=22939/band-of-unanswered-prayers
			EquipNewItem(pTarget, 22939, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=23027/warmth-of-forgiveness
			EquipNewItem(pTarget, 23027, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=23047/eye-of-the-dead
			EquipNewItem(pTarget, 23047, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		default:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=22988/the-end-of-dreams
			EquipNewItem(pTarget, 22988, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=22749/enchant-weapon-spell-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2504);

			//https://classic.wowhead.com/item=23049/sapphirons-left-eye
			EquipNewItem(pTarget, 23049, EQUIPMENT_SLOT_OFFHAND);

			//https://classic.wowhead.com/item=21603/wand-of-qiraji-nobility
			EquipNewItem(pTarget, 21603, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=23035/preceptors-hat
			EquipNewItem(pTarget, 23035, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/spell=22844/arcanum-of-focus
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2544);

			//https://classic.wowhead.com/item=18814/choker-of-the-fire-lord
			EquipNewItem(pTarget, 18814, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=22983/rime-covered-mantle
			EquipNewItem(pTarget, 22983, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/spell=29467/power-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2721);

			//https://classic.wowhead.com/item=22731/cloak-of-the-devoured
			EquipNewItem(pTarget, 22731, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2621);

			//https://classic.wowhead.com/item=23220/crystal-webbed-robe
			EquipNewItem(pTarget, 23220, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1891);

			//https://classic.wowhead.com/item=21611/burrower-bracers
			EquipNewItem(pTarget, 21611, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=23801/enchant-bracer-mana-regeneration
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 2565);

			//https://classic.wowhead.com/item=19407/ebony-flame-gloves
			EquipNewItem(pTarget, 19407, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25073/enchant-gloves-shadow-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2614);

			//https://classic.wowhead.com/item=22730/eyestalk-waist-cord
			EquipNewItem(pTarget, 22730, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=19133/fel-infused-leggings
			EquipNewItem(pTarget, 19133, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/spell=22844/arcanum-of-focus
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2544);

			//https://classic.wowhead.com/item=21600/boots-of-epiphany
			EquipNewItem(pTarget, 21600, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=21709/ring-of-the-fallen-god
			EquipNewItem(pTarget, 21709, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=23031/band-of-the-inevitable
			EquipNewItem(pTarget, 23031, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=19379/neltharions-tear
			EquipNewItem(pTarget, 19379, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=23046/the-restrained-essence-of-sapphiron
			EquipNewItem(pTarget, 23046, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		}

		break;
	}
	case CLASS_SHAMAN:
	{
		switch (role)
		{
		case ROLE_TANK:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=2243/hand-of-edward-the-odd
			EquipNewItem(pTarget, 2243, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=22749/enchant-weapon-spell-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2504);

			//https://classic.wowhead.com/item=22819/shield-of-condemnation
			EquipNewItem(pTarget, 22819, EQUIPMENT_SLOT_OFFHAND);
			//https://classic.wowhead.com/spell=20017/enchant-shield-greater-stamina
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
			Enchant(pTarget, item, 929);

			//https://classic.wowhead.com/item=22395/totem-of-rage
			EquipNewItem(pTarget, 22395, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=22466/earthshatter-headpiece
			EquipNewItem(pTarget, 22466, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/item=19786/vodouisants-vigilant-embrace
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2587);

			//https://classic.wowhead.com/item=18814/choker-of-the-fire-lord
			EquipNewItem(pTarget, 18814, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=22467/earthshatter-spaulders
			EquipNewItem(pTarget, 22467, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/item=23545/power-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2721);

			//https://classic.wowhead.com/item=22731/cloak-of-the-devoured
			EquipNewItem(pTarget, 22731, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25086/enchant-cloak-dodge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2622);

			//https://classic.wowhead.com/item=22464/earthshatter-tunic
			EquipNewItem(pTarget, 22464, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20026/enchant-chest-major-health
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1892);

			//https://classic.wowhead.com/item=22471/earthshatter-wristguards
			EquipNewItem(pTarget, 22471, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=23802/enchant-bracer-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 2566);

			//https://classic.wowhead.com/item=22469/earthshatter-handguards
			EquipNewItem(pTarget, 22469, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25079/enchant-gloves-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2617);

			//https://classic.wowhead.com/item=22470/earthshatter-girdle
			EquipNewItem(pTarget, 22470, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=22465/earthshatter-legguards
			EquipNewItem(pTarget, 22465, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/item=19786/vodouisants-vigilant-embrace
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2587);

			//https://classic.wowhead.com/item=22468/earthshatter-boots
			EquipNewItem(pTarget, 22468, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=21709/ring-of-the-fallen-god
			EquipNewItem(pTarget, 21709, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=21210/signet-ring-of-the-bronze-dragonflight
			EquipNewItem(pTarget, 21210, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=23046/the-restrained-essence-of-sapphiron
			EquipNewItem(pTarget, 23046, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=19379/neltharions-tear
			EquipNewItem(pTarget, 19379, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		case ROLE_HEALER:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=23056/hammer-of-the-twisting-nether
			EquipNewItem(pTarget, 23056, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=22750/enchant-weapon-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2505);

			//https://classic.wowhead.com/item=22819/shield-of-condemnation
			EquipNewItem(pTarget, 22819, EQUIPMENT_SLOT_OFFHAND);
			//https://classic.wowhead.com/spell=20017/enchant-shield-greater-stamina
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
			Enchant(pTarget, item, 929);

			//https://classic.wowhead.com/item=22396/totem-of-life
			EquipNewItem(pTarget, 22396, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=22466/earthshatter-headpiece
			EquipNewItem(pTarget, 22466, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/spell=24163/vodouisants-vigilant-embrace
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2587);

			//https://classic.wowhead.com/item=21712/amulet-of-the-fallen-god
			EquipNewItem(pTarget, 21712, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=22467/earthshatter-spaulders
			EquipNewItem(pTarget, 22467, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/spell=29475/resilience-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2715);

			//https://classic.wowhead.com/item=21583/cloak-of-clarity
			EquipNewItem(pTarget, 21583, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2621);

			//https://classic.wowhead.com/item=22464/earthshatter-tunic
			EquipNewItem(pTarget, 22464, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20028/enchant-chest-major-mana
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1893);

			//https://classic.wowhead.com/item=22471/earthshatter-wristguards
			EquipNewItem(pTarget, 22471, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=23802/enchant-bracer-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 2566);

			//https://classic.wowhead.com/item=22469/earthshatter-handguards
			EquipNewItem(pTarget, 22469, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25079/enchant-gloves-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2617);

			//https://classic.wowhead.com/item=21582/grasp-of-the-old-god
			EquipNewItem(pTarget, 21582, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=22465/earthshatter-legguards
			EquipNewItem(pTarget, 22465, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/spell=24163/vodouisants-vigilant-embrace
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2587);

			//https://classic.wowhead.com/item=22468/earthshatter-boots
			EquipNewItem(pTarget, 22468, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=23065/ring-of-the-earthshatterer
			EquipNewItem(pTarget, 23065, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=21620/ring-of-the-martyr
			EquipNewItem(pTarget, 21620, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=19395/rejuvenating-gem
			EquipNewItem(pTarget, 19395, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=23027/warmth-of-forgiveness
			EquipNewItem(pTarget, 23027, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		case ROLE_MELEE_DPS:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=2243/hand-of-edward-the-odd
			EquipNewItem(pTarget, 2243, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=22749/enchant-weapon-spell-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2504);

			//https://classic.wowhead.com/item=23049/sapphirons-left-eye
			EquipNewItem(pTarget, 23049, EQUIPMENT_SLOT_OFFHAND);

			//https://classic.wowhead.com/item=22395/totem-of-rage
			EquipNewItem(pTarget, 22395, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=19375/mishundare-circlet-of-the-mind-flayer
			EquipNewItem(pTarget, 19375, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/item=19786/vodouisants-vigilant-embrace
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2587);

			//https://classic.wowhead.com/item=23057/gem-of-trapped-innocents
			EquipNewItem(pTarget, 23057, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=22983/rime-covered-mantle
			EquipNewItem(pTarget, 22983, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/item=23545/power-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2721);

			//https://classic.wowhead.com/item=23050/cloak-of-the-necropolis
			EquipNewItem(pTarget, 23050, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2621);

			//https://classic.wowhead.com/item=21374/stormcallers-hauberk
			EquipNewItem(pTarget, 21374, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1891);

			//https://classic.wowhead.com/item=23021/the-soul-harvesters-bindings
			EquipNewItem(pTarget, 23021, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=20008/enchant-bracer-greater-intellect
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 1883);

			//https://classic.wowhead.com/item=21689/gloves-of-ebru
			EquipNewItem(pTarget, 21689, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25080/enchant-gloves-superior-agility
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2564);

			//https://classic.wowhead.com/item=22730/eyestalk-waist-cord
			EquipNewItem(pTarget, 22730, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=23665/leggings-of-elemental-fury
			EquipNewItem(pTarget, 23665, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/item=19786/vodouisants-vigilant-embrace
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2587);

			//https://classic.wowhead.com/item=21600/boots-of-epiphany
			EquipNewItem(pTarget, 21600, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=21709/ring-of-the-fallen-god
			EquipNewItem(pTarget, 21709, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=20632/mindtear-band
			EquipNewItem(pTarget, 20632, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=19379/neltharions-tear
			EquipNewItem(pTarget, 19379, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=23046/the-restrained-essence-of-sapphiron
			EquipNewItem(pTarget, 23046, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		default:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=22799/soulseeker
			EquipNewItem(pTarget, 22799, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=22749/enchant-weapon-spell-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2504);

			//https://classic.wowhead.com/item=23199/totem-of-the-storm
			EquipNewItem(pTarget, 23199, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=19375/mishundare-circlet-of-the-mind-flayer
			EquipNewItem(pTarget, 19375, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/spell=24163/vodouisants-vigilant-embrace
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2587);

			//https://classic.wowhead.com/item=21608/amulet-of-veknilash
			EquipNewItem(pTarget, 21608, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=22983/rime-covered-mantle
			EquipNewItem(pTarget, 22983, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/spell=29467/power-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2721);

			//https://classic.wowhead.com/item=23050/cloak-of-the-necropolis
			EquipNewItem(pTarget, 23050, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2621);

			//https://classic.wowhead.com/item=23220/crystal-webbed-robe
			EquipNewItem(pTarget, 23220, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1891);

			//https://classic.wowhead.com/item=21186/rockfury-bracers
			EquipNewItem(pTarget, 21186, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=20008/enchant-bracer-greater-intellect
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 1883);

			//https://classic.wowhead.com/item=21585/dark-storm-gauntlets
			EquipNewItem(pTarget, 21585, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25079/enchant-gloves-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2617);

			//https://classic.wowhead.com/item=22730/eyestalk-waist-cord
			EquipNewItem(pTarget, 22730, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=23070/leggings-of-polarity
			EquipNewItem(pTarget, 23070, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/spell=24163/vodouisants-vigilant-embrace
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2587);

			//https://classic.wowhead.com/item=21600/boots-of-epiphany
			EquipNewItem(pTarget, 21600, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=21709/ring-of-the-fallen-god
			EquipNewItem(pTarget, 21709, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=23031/band-of-the-inevitable
			EquipNewItem(pTarget, 23031, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=19379/neltharions-tear
			EquipNewItem(pTarget, 19379, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=23046/the-restrained-essence-of-sapphiron
			EquipNewItem(pTarget, 23046, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		}

		break;
	}
	case CLASS_MAGE:
	{
		Item* item = nullptr;

		//https://classic.wowhead.com/item=22807/wraith-blade
		EquipNewItem(pTarget, 22807, EQUIPMENT_SLOT_MAINHAND);
		//https://classic.wowhead.com/spell=22749/enchant-weapon-spell-power
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
		Enchant(pTarget, item, 2504);

		//https://classic.wowhead.com/item=23049/sapphirons-left-eye
		EquipNewItem(pTarget, 23049, EQUIPMENT_SLOT_OFFHAND);

		//https://classic.wowhead.com/item=22821/doomfinger
		EquipNewItem(pTarget, 22821, EQUIPMENT_SLOT_RANGED);

		//https://classic.wowhead.com/item=22498/frostfire-circlet
		EquipNewItem(pTarget, 22498, EQUIPMENT_SLOT_HEAD);
		//https://classic.wowhead.com/spell=24164/presence-of-sight
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
		Enchant(pTarget, item, 2588);

		//https://classic.wowhead.com/item=23057/gem-of-trapped-innocents
		EquipNewItem(pTarget, 23057, EQUIPMENT_SLOT_NECK);

		//https://classic.wowhead.com/item=22983/rime-covered-mantle
		EquipNewItem(pTarget, 22983, EQUIPMENT_SLOT_SHOULDERS);
		//https://classic.wowhead.com/spell=29467/power-of-the-scourge
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
		Enchant(pTarget, item, 2721);

		//https://classic.wowhead.com/item=23050/cloak-of-the-necropolis
		EquipNewItem(pTarget, 23050, EQUIPMENT_SLOT_BACK);
		//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
		Enchant(pTarget, item, 2621);

		//https://classic.wowhead.com/item=22496/frostfire-robe
		EquipNewItem(pTarget, 22496, EQUIPMENT_SLOT_CHEST);
		//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
		Enchant(pTarget, item, 1891);

		//https://classic.wowhead.com/item=23021/the-soul-harvesters-bindings
		EquipNewItem(pTarget, 23021, EQUIPMENT_SLOT_WRISTS);
		//https://classic.wowhead.com/spell=20008/enchant-bracer-greater-intellect
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
		Enchant(pTarget, item, 1883);

		//https://classic.wowhead.com/item=21585/dark-storm-gauntlets
		EquipNewItem(pTarget, 21585, EQUIPMENT_SLOT_HANDS);
		//https://classic.wowhead.com/spell=25078/enchant-gloves-fire-power
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
		Enchant(pTarget, item, 2616);

		//https://classic.wowhead.com/item=22730/eyestalk-waist-cord
		EquipNewItem(pTarget, 22730, EQUIPMENT_SLOT_WAIST);

		//https://classic.wowhead.com/item=23070/leggings-of-polarity
		EquipNewItem(pTarget, 23070, EQUIPMENT_SLOT_LEGS);
		//https://classic.wowhead.com/spell=24164/presence-of-sight
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
		Enchant(pTarget, item, 2588);

		//https://classic.wowhead.com/item=22500/frostfire-sandals
		EquipNewItem(pTarget, 22500, EQUIPMENT_SLOT_FEET);
		//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
		Enchant(pTarget, item, 911);

		//https://classic.wowhead.com/item=23062/frostfire-ring
		EquipNewItem(pTarget, 23062, EQUIPMENT_SLOT_FINGER1);

		//https://classic.wowhead.com/item=23237/ring-of-the-eternal-flame
		EquipNewItem(pTarget, 23237, EQUIPMENT_SLOT_FINGER2);

		//https://classic.wowhead.com/item=19379/neltharions-tear
		EquipNewItem(pTarget, 19379, EQUIPMENT_SLOT_TRINKET1);

		//https://classic.wowhead.com/item=23046/the-restrained-essence-of-sapphiron
		EquipNewItem(pTarget, 23046, EQUIPMENT_SLOT_TRINKET2);

		break;
	}
	case CLASS_WARLOCK:
	{
		Item* item = nullptr;

		//https://classic.wowhead.com/item=22807/wraith-blade
		EquipNewItem(pTarget, 22807, EQUIPMENT_SLOT_MAINHAND);
		//https://classic.wowhead.com/spell=22749/enchant-weapon-spell-power
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
		Enchant(pTarget, item, 2504);

		//https://classic.wowhead.com/item=23049/sapphirons-left-eye
		EquipNewItem(pTarget, 23049, EQUIPMENT_SLOT_OFFHAND);

		//https://classic.wowhead.com/item=22820/wand-of-fates
		EquipNewItem(pTarget, 22820, EQUIPMENT_SLOT_RANGED);

		//https://classic.wowhead.com/item=22506/plagueheart-circlet
		EquipNewItem(pTarget, 22506, EQUIPMENT_SLOT_HEAD);
		//https://classic.wowhead.com/spell=24165/hoodoo-hex
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
		Enchant(pTarget, item, 2589);

		//https://classic.wowhead.com/item=23057/gem-of-trapped-innocents
		EquipNewItem(pTarget, 23057, EQUIPMENT_SLOT_NECK);

		//https://classic.wowhead.com/item=22507/plagueheart-shoulderpads
		EquipNewItem(pTarget, 22507, EQUIPMENT_SLOT_SHOULDERS);
		//https://classic.wowhead.com/spell=29467/power-of-the-scourge
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
		Enchant(pTarget, item, 2721);

		//https://classic.wowhead.com/item=23050/cloak-of-the-necropolis
		EquipNewItem(pTarget, 23050, EQUIPMENT_SLOT_BACK);
		//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
		Enchant(pTarget, item, 2621);

		//https://classic.wowhead.com/item=22504/plagueheart-robe
		EquipNewItem(pTarget, 22504, EQUIPMENT_SLOT_CHEST);
		//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
		Enchant(pTarget, item, 1891);

		//https://classic.wowhead.com/item=21186/rockfury-bracers
		EquipNewItem(pTarget, 21186, EQUIPMENT_SLOT_WRISTS);
		//https://classic.wowhead.com/spell=20008/enchant-bracer-greater-intellect
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
		Enchant(pTarget, item, 1883);

		//https://classic.wowhead.com/item=21585/dark-storm-gauntlets
		EquipNewItem(pTarget, 21585, EQUIPMENT_SLOT_HANDS);
		//https://classic.wowhead.com/spell=25073/enchant-gloves-shadow-power
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
		Enchant(pTarget, item, 2614);

		//https://classic.wowhead.com/item=22730/eyestalk-waist-cord
		EquipNewItem(pTarget, 22730, EQUIPMENT_SLOT_WAIST);

		//https://classic.wowhead.com/item=23070/leggings-of-polarity
		EquipNewItem(pTarget, 23070, EQUIPMENT_SLOT_LEGS);
		//https://classic.wowhead.com/spell=24165/hoodoo-hex
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
		Enchant(pTarget, item, 2589);

		//https://classic.wowhead.com/item=22508/plagueheart-sandals
		EquipNewItem(pTarget, 22508, EQUIPMENT_SLOT_FEET);
		//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
		item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
		Enchant(pTarget, item, 911);

		//https://classic.wowhead.com/item=21709/ring-of-the-fallen-god
		EquipNewItem(pTarget, 21709, EQUIPMENT_SLOT_FINGER1);

		//https://classic.wowhead.com/item=23031/band-of-the-inevitable
		EquipNewItem(pTarget, 23031, EQUIPMENT_SLOT_FINGER2);

		//https://classic.wowhead.com/item=19379/neltharions-tear
		EquipNewItem(pTarget, 19379, EQUIPMENT_SLOT_TRINKET1);

		//https://classic.wowhead.com/item=23046/the-restrained-essence-of-sapphiron
		EquipNewItem(pTarget, 23046, EQUIPMENT_SLOT_TRINKET2);

		break;
	}
	case CLASS_DRUID:
	{
		switch (role)
		{
		case ROLE_TANK:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=21268/blessed-qiraji-war-hammer
			EquipNewItem(pTarget, 21268, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=23799/enchant-weapon-strength
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2563);

			//https://classic.wowhead.com/item=13385/tome-of-knowledge
			EquipNewItem(pTarget, 13385, EQUIPMENT_SLOT_OFFHAND);

			//https://classic.wowhead.com/item=23198/idol-of-brutality
			EquipNewItem(pTarget, 23198, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=21693/guise-of-the-devourer
			EquipNewItem(pTarget, 21693, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/item=18329/arcanum-of-rapidity
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2543);

			//https://classic.wowhead.com/item=23053/stormrages-talisman-of-seething
			EquipNewItem(pTarget, 23053, EQUIPMENT_SLOT_NECK);

			if (pTarget->GetTeam() == ALLIANCE)
			{
				//https://classic.wowhead.com/item=20059/highlanders-leather-shoulders
				EquipNewItem(pTarget, 20059, EQUIPMENT_SLOT_SHOULDERS);
				//https://classic.wowhead.com/item=23548/might-of-the-scourge
				item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
				Enchant(pTarget, item, 2717);
			}
			else
			{
				//https://classic.wowhead.com/item=20175/defilers-lizardhide-shoulders
				EquipNewItem(pTarget, 20175, EQUIPMENT_SLOT_SHOULDERS);
				//https://classic.wowhead.com/item=23548/might-of-the-scourge
				item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
				Enchant(pTarget, item, 2717);
			}

			//https://classic.wowhead.com/item=22938/cryptfiend-silk-cloak
			EquipNewItem(pTarget, 22938, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25086/enchant-cloak-dodge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2622);

			//https://classic.wowhead.com/item=23226/ghoul-skin-tunic
			EquipNewItem(pTarget, 23226, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1891);

			//https://classic.wowhead.com/item=21602/qiraji-execution-bracers
			EquipNewItem(pTarget, 21602, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=20010/enchant-bracer-superior-strength
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 1885);

			//https://classic.wowhead.com/item=21605/gloves-of-the-hidden-temple
			EquipNewItem(pTarget, 21605, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25072/enchant-gloves-threat
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2613);

			//https://classic.wowhead.com/item=21586/belt-of-never-ending-agony
			EquipNewItem(pTarget, 21586, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=23071/leggings-of-apocalypse
			EquipNewItem(pTarget, 23071, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/item=18329/arcanum-of-rapidity
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2543);

			//https://classic.wowhead.com/item=19381/boots-of-the-shadow-flame
			EquipNewItem(pTarget, 19381, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=23018/signet-of-the-fallen-defender
			EquipNewItem(pTarget, 23018, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=17063/band-of-accuria
			EquipNewItem(pTarget, 17063, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=22954/kiss-of-the-spider
			EquipNewItem(pTarget, 22954, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=23041/slayers-crest
			EquipNewItem(pTarget, 23041, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		case ROLE_HEALER:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=23056/hammer-of-the-twisting-nether
			EquipNewItem(pTarget, 23056, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=22750/enchant-weapon-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2505);

			//https://classic.wowhead.com/item=23048/sapphirons-right-eye
			EquipNewItem(pTarget, 23048, EQUIPMENT_SLOT_OFFHAND);

			//https://classic.wowhead.com/item=22399/idol-of-health
			EquipNewItem(pTarget, 22399, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=20628/deviate-growth-cap
			EquipNewItem(pTarget, 20628, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/spell=24168/animists-caress
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2591);

			//https://classic.wowhead.com/item=21712/amulet-of-the-fallen-god
			EquipNewItem(pTarget, 21712, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=22491/dreamwalker-spaulders
			EquipNewItem(pTarget, 22491, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/spell=29475/resilience-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2715);

			//https://classic.wowhead.com/item=22960/cloak-of-suturing
			EquipNewItem(pTarget, 22960, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2621);

			//https://classic.wowhead.com/item=22488/dreamwalker-tunic
			EquipNewItem(pTarget, 22488, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1891);

			//https://classic.wowhead.com/item=21604/bracelets-of-royal-redemption
			EquipNewItem(pTarget, 21604, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=23802/enchant-bracer-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 2566);

			//https://classic.wowhead.com/item=22493/dreamwalker-handguards
			EquipNewItem(pTarget, 22493, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25079/enchant-gloves-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2617);

			//https://classic.wowhead.com/item=21582/grasp-of-the-old-god
			EquipNewItem(pTarget, 21582, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=22489/dreamwalker-legguards
			EquipNewItem(pTarget, 22489, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/spell=24168/animists-caress
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2591);

			//https://classic.wowhead.com/item=22492/dreamwalker-boots
			EquipNewItem(pTarget, 22492, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=21620/ring-of-the-martyr
			EquipNewItem(pTarget, 21620, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=22939/band-of-unanswered-prayers
			EquipNewItem(pTarget, 22939, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=23047/eye-of-the-dead
			EquipNewItem(pTarget, 23047, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=23027/warmth-of-forgiveness
			EquipNewItem(pTarget, 23027, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		case ROLE_MELEE_DPS:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=22988/the-end-of-dreams
			EquipNewItem(pTarget, 22988, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=23799/enchant-weapon-strength
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2563);

			//https://classic.wowhead.com/item=13385/tome-of-knowledge
			EquipNewItem(pTarget, 13385, EQUIPMENT_SLOT_OFFHAND);

			//https://classic.wowhead.com/item=22397/idol-of-ferocity
			EquipNewItem(pTarget, 22397, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=8345/wolfshead-helm
			EquipNewItem(pTarget, 8345, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/item=11645/lesser-arcanum-of-voracity
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 1506);

			//https://classic.wowhead.com/item=19377/prestors-talisman-of-connivery
			EquipNewItem(pTarget, 19377, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=21665/mantle-of-wicked-revenge
			EquipNewItem(pTarget, 21665, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/spell=29483/might-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2717);

			//https://classic.wowhead.com/item=21701/cloak-of-concentrated-hatred
			EquipNewItem(pTarget, 21701, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2621);

			//https://classic.wowhead.com/item=21680/vest-of-swift-execution
			EquipNewItem(pTarget, 21680, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1891);

			//https://classic.wowhead.com/item=21602/qiraji-execution-bracers
			EquipNewItem(pTarget, 21602, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=20010/enchant-bracer-superior-strength
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 1885);

			//https://classic.wowhead.com/item=21672/gloves-of-enforcement
			EquipNewItem(pTarget, 21672, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25080/enchant-gloves-superior-agility
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2564);

			//https://classic.wowhead.com/item=21586/belt-of-never-ending-agony
			EquipNewItem(pTarget, 21586, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=23071/leggings-of-apocalypse
			EquipNewItem(pTarget, 23071, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/item=11645/lesser-arcanum-of-voracity
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 1506);

			//https://classic.wowhead.com/item=21493/boots-of-the-vanguard
			EquipNewItem(pTarget, 21493, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=23038/band-of-unnatural-forces
			EquipNewItem(pTarget, 23038, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=17063/band-of-accuria
			EquipNewItem(pTarget, 17063, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=22954/kiss-of-the-spider
			EquipNewItem(pTarget, 22954, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=23041/slayers-crest
			EquipNewItem(pTarget, 23041, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		default:
		{
			Item* item = nullptr;

			//https://classic.wowhead.com/item=22799/soulseeker
			EquipNewItem(pTarget, 22799, EQUIPMENT_SLOT_MAINHAND);
			//https://classic.wowhead.com/spell=22749/enchant-weapon-spell-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
			Enchant(pTarget, item, 2504);

			//https://classic.wowhead.com/item=23197/idol-of-the-moon
			EquipNewItem(pTarget, 23197, EQUIPMENT_SLOT_RANGED);

			//https://classic.wowhead.com/item=19375/mishundare-circlet-of-the-mind-flayer
			EquipNewItem(pTarget, 19375, EQUIPMENT_SLOT_HEAD);
			//https://classic.wowhead.com/item=18330/arcanum-of-focus
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
			Enchant(pTarget, item, 2544);

			//https://classic.wowhead.com/item=21608/amulet-of-veknilash
			EquipNewItem(pTarget, 21608, EQUIPMENT_SLOT_NECK);

			//https://classic.wowhead.com/item=22983/rime-covered-mantle
			EquipNewItem(pTarget, 22983, EQUIPMENT_SLOT_SHOULDERS);
			//https://classic.wowhead.com/spell=29467/power-of-the-scourge
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_SHOULDERS);
			Enchant(pTarget, item, 2721);

			//https://classic.wowhead.com/item=23050/cloak-of-the-necropolis
			EquipNewItem(pTarget, 23050, EQUIPMENT_SLOT_BACK);
			//https://classic.wowhead.com/spell=25084/enchant-cloak-subtlety
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_BACK);
			Enchant(pTarget, item, 2621);

			//https://classic.wowhead.com/item=19682/bloodvine-vest
			EquipNewItem(pTarget, 19682, EQUIPMENT_SLOT_CHEST);
			//https://classic.wowhead.com/spell=20025/enchant-chest-greater-stats
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
			Enchant(pTarget, item, 1891);

			//https://classic.wowhead.com/item=21186/rockfury-bracers
			EquipNewItem(pTarget, 21186, EQUIPMENT_SLOT_WRISTS);
			//https://classic.wowhead.com/spell=20008/enchant-bracer-greater-intellect
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_WRISTS);
			Enchant(pTarget, item, 1883);

			//https://classic.wowhead.com/item=21585/dark-storm-gauntlets
			EquipNewItem(pTarget, 21585, EQUIPMENT_SLOT_HANDS);
			//https://classic.wowhead.com/spell=25079/enchant-gloves-healing-power
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS);
			Enchant(pTarget, item, 2617);

			//https://classic.wowhead.com/item=22730/eyestalk-waist-cord
			EquipNewItem(pTarget, 22730, EQUIPMENT_SLOT_WAIST);

			//https://classic.wowhead.com/item=19683/bloodvine-leggings
			EquipNewItem(pTarget, 19683, EQUIPMENT_SLOT_LEGS);
			//https://classic.wowhead.com/item=18330/arcanum-of-focus
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS);
			Enchant(pTarget, item, 2544);

			//https://classic.wowhead.com/item=19684/bloodvine-boots
			EquipNewItem(pTarget, 19684, EQUIPMENT_SLOT_FEET);
			//https://classic.wowhead.com/spell=13890/enchant-boots-minor-speed
			item = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_FEET);
			Enchant(pTarget, item, 911);

			//https://classic.wowhead.com/item=21709/ring-of-the-fallen-god
			EquipNewItem(pTarget, 21709, EQUIPMENT_SLOT_FINGER1);

			//https://classic.wowhead.com/item=23031/band-of-the-inevitable
			EquipNewItem(pTarget, 23031, EQUIPMENT_SLOT_FINGER2);

			//https://classic.wowhead.com/item=19379/neltharions-tear
			EquipNewItem(pTarget, 19379, EQUIPMENT_SLOT_TRINKET1);

			//https://classic.wowhead.com/item=23046/the-restrained-essence-of-sapphiron
			EquipNewItem(pTarget, 23046, EQUIPMENT_SLOT_TRINKET2);

			break;
		}
		}

		break;
	}
	}
}

void CombatBotBaseAI::GiveBags(Player* pTarget)
{
	uint8 count_empty_bag_slots = 0;
	for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
		if (nullptr == dynamic_cast<Bag*>(pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, i)))
			count_empty_bag_slots++;
	if (pTarget->getClass() != CLASS_HUNTER)
		pTarget->StoreNewItemInBestSlots(23162, count_empty_bag_slots);
	else
		pTarget->StoreNewItemInBestSlots(23162, count_empty_bag_slots - 1);
}

void CombatBotBaseAI::GiveAmmoSortHelper(Player* pTarget)
{
	for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
	{
		if (Item* pItem = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
		{
			uint16 eDest;
			uint8 msg = pTarget->CanEquipItem(NULL_SLOT, eDest, pItem, false);
			if (msg == EQUIP_ERR_OK)
			{
				pTarget->RemoveItem(INVENTORY_SLOT_BAG_0, i, true);
				pTarget->EquipItem(eDest, pItem, true);
			}
			else
			{
				ItemPosCountVec sDest;
				msg = pTarget->CanStoreItem(NULL_BAG, NULL_SLOT, sDest, pItem, false);
				if (msg == EQUIP_ERR_OK)
				{
					pTarget->RemoveItem(INVENTORY_SLOT_BAG_0, i, true);
					pItem = pTarget->StoreItem(sDest, pItem, true);
				}
				msg = pTarget->CanUseAmmo(pItem->GetEntry());
				if (msg == EQUIP_ERR_OK)
					pTarget->SetAmmo(pItem->GetEntry());
			}
		}
	}
}

uint32 CombatBotBaseAI::SelectAmmo(const Player* pTarget)
{
	if (const Item* pItem = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED))
	{
		if (pItem->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_BOW || pItem->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_CROSSBOW)
		{
			if (pTarget->GetLevel() >= 54)
				return 12654;	//https://classic.wowhead.com/item=12654/doomshot
			if (pTarget->GetLevel() >= 52)
				return 18042;	//https://classic.wowhead.com/item=18042/thorium-headed-arrow
			if (pTarget->GetLevel() == 51)
				return 19316;	//https://classic.wowhead.com/item=19316/ice-threaded-arrow
			if (pTarget->GetLevel() >= 37)
				return 10579;	//https://classic.wowhead.com/item=10579/explosive-arrow
			if (pTarget->GetLevel() >= 35)
				return 9399;	//https://classic.wowhead.com/item=9399/precision-arrow
			if (pTarget->GetLevel() >= 30)
				return 3464;	//https://classic.wowhead.com/item=3464/feathered-arrow
			if (pTarget->GetLevel() >= 25)
				return 3030;	//https://classic.wowhead.com/item=3030/razor-arrow
			if (pTarget->GetLevel() >= 10)
				return 2515;	//https://classic.wowhead.com/item=2515/sharp-arrow
			if (pTarget->GetLevel() >= 1)
				return 2512;	//https://classic.wowhead.com/item=2512/rough-arrow
		}
		else if (pItem->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_GUN)
		{
			if (pTarget->GetLevel() >= 56)
				return 13377;	//https://classic.wowhead.com/item=13377/miniature-cannon-balls
			if (pTarget->GetLevel() >= 47)
				return 11630;	//https://classic.wowhead.com/item=11630/rockshard-pellets
			if (pTarget->GetLevel() >= 44)
				return 10513;	//https://classic.wowhead.com/item=10513/mithril-gyro-shot
			if (pTarget->GetLevel() >= 40)
				return 11284;	//https://classic.wowhead.com/item=11284/accurate-slugs
			if (pTarget->GetLevel() >= 37)
				return 10512;	//https://classic.wowhead.com/item=10512/hi-impact-mithril-slugs
			if (pTarget->GetLevel() >= 30)
				return 3465;	//https://classic.wowhead.com/item=3465/exploding-shot
			if (pTarget->GetLevel() >= 25)
				return 3033;	//https://classic.wowhead.com/item=3033/solid-shot
			if (pTarget->GetLevel() >= 15)
				return 8068;	//https://classic.wowhead.com/item=8068/crafted-heavy-shot
			if (pTarget->GetLevel() >= 13)
				return 5568;	//https://classic.wowhead.com/item=5568/smooth-pebble
			if (pTarget->GetLevel() >= 10)
				return 2519;	//https://classic.wowhead.com/item=2519/heavy-shot
			if (pTarget->GetLevel() >= 1)
				return 4960;	//https://classic.wowhead.com/item=4960/flash-pellet
		}
	}

	return 0;
}

uint32 CombatBotBaseAI::SelectQuiver(const Player* pTarget)
{
	if (const Item* pItem = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED))
	{
		if (pItem->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_BOW || pItem->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_CROSSBOW)
		{
			if (pTarget->GetLevel() >= 60)
				return 18714;	//https://classic.wowhead.com/item=18714/ancient-sinew-wrapped-lamina
			if (pTarget->GetLevel() >= 55 && pTarget->GetLevel() <= 59)
				return 19319;	//https://classic.wowhead.com/item=19319/harpy-hide-quiver
			if (pTarget->GetLevel() >= 50 && pTarget->GetLevel() <= 54)
				return 2662;	//https://classic.wowhead.com/item=2662/ribblys-quiver
			if (pTarget->GetLevel() >= 40 && pTarget->GetLevel() <= 49)
				return 8217;	//https://classic.wowhead.com/item=8217/quickdraw-quiver
			if (pTarget->GetLevel() >= 30 && pTarget->GetLevel() <= 39)
				return 7371;	//https://classic.wowhead.com/item=7371/heavy-quiver
			if (pTarget->GetLevel() >= 18 && pTarget->GetLevel() <= 29)
				return 3605;	//https://classic.wowhead.com/item=3605/quiver-of-the-night-watch
			if (pTarget->GetLevel() >= 10 && pTarget->GetLevel() <= 17)
				return 11362;	//https://classic.wowhead.com/item=11362/medium-quiver
			if (pTarget->GetLevel() <= 9)
				return 5439;	//https://classic.wowhead.com/item=5439/small-quiver
		}
		else if (pItem->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_GUN)
		{
			if (pTarget->GetLevel() >= 55)
				return 19320;	//https://classic.wowhead.com/item=19320/gnoll-skin-bandolier
			if (pTarget->GetLevel() >= 50 && pTarget->GetLevel() <= 54)
				return 2663;	//https://classic.wowhead.com/item=2663/ribblys-bandolier
			if (pTarget->GetLevel() >= 40 && pTarget->GetLevel() <= 49)
				return 8218;	//https://classic.wowhead.com/item=8218/thick-leather-ammo-pouch
			if (pTarget->GetLevel() >= 30 && pTarget->GetLevel() <= 39)
				return 7372;	//https://classic.wowhead.com/item=7372/heavy-leather-ammo-pouch
			if (pTarget->GetLevel() >= 18 && pTarget->GetLevel() <= 29)
				return 3604;	//https://classic.wowhead.com/item=3604/bandolier-of-the-night-watch
			if (pTarget->GetLevel() >= 10 && pTarget->GetLevel() <= 17)
				return 11363;	//https://classic.wowhead.com/item=11363/medium-shot-pouch
			if (pTarget->GetLevel() <= 9)
				return 5441;	//https://classic.wowhead.com/item=5441/small-shot-pouch
		}
	}

	return 0;
}

void CombatBotBaseAI::GiveAmmo(Player* pTarget, const uint32 stacks)
{
	if (!(pTarget->getClass() == CLASS_HUNTER || pTarget->getClass() == CLASS_WARRIOR || pTarget->getClass() == CLASS_ROGUE))
		return;

	// Check for quiver
	auto pQuiver = dynamic_cast<Bag*>(pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, INVENTORY_SLOT_BAG_END - 1));

	// If Quiver isn't already equipped then equip it
	if (pTarget->getClass() == CLASS_HUNTER && !pQuiver)
		if (const uint32 Quiver = SelectQuiver(pTarget))
			EquipNewItem(pTarget, Quiver, INVENTORY_SLOT_BAG_END - 1);

	// Reheck for quiver
	pQuiver = dynamic_cast<Bag*>(pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, INVENTORY_SLOT_BAG_END - 1));

	uint32 amount = 0;
	if (stacks)
		amount = stacks;
	else if (pQuiver && pTarget->getClass() == CLASS_HUNTER)
		amount = pQuiver->GetBagSize();
	else if (pTarget->getClass() == CLASS_WARRIOR || pTarget->getClass() == CLASS_ROGUE)
		amount = 1;

	// Give the target the requested amount of Arrows
	if (amount)
	{
		if (const uint32 Ammo = SelectAmmo(pTarget))
		{
			pTarget->StoreNewItemInBestSlots(Ammo, amount * 200);
			GiveAmmoSortHelper(pTarget);
		}
	}
}

bool CombatBotBaseAI::InitializeCharacter(Player* pTarget, Player* pLeader, const uint32 pTargetLevel, const CombatBotRoles role)
{
	// Control checks
	if (!pTarget)
		return false;

	// Match Target's level
	if (pTarget->GetLevel() != pTargetLevel)
		pTarget->GiveLevel(pTargetLevel);

	// Reset all spells
	pTarget->resetSpells();

	// Empty the content of the target's inventory
	ClearInventory(pTarget);

	// Learn Talents
	LearnTalents(pTarget, role);

	// Learn Spells
	LearnSpellsAndSkills(pTarget);

	// Max level sets
	InitializeSets(pTarget, pLeader, role);

	// Give Bags to everybody
	GiveBags(pTarget);

	// Give Ammo to those who need it
	GiveAmmo(pTarget);

	return true;
}

/*********************************************************/
/***					COMBAT                         ***/
/*********************************************************/

uint8 CombatBotBaseAI::GetAttackersInRangeCount(const float range) const
{
	if (me->getAttackers().empty())
		return 0;

	uint8 count = 0;
	for (const auto& pTarget : me->getAttackers())
		if (me->GetCombatDistance(pTarget) <= range)
			count++;

	return count;
}

uint32 CombatBotBaseAI::NumberOfGroupAttackers() const
{
	Group* pGroup = me->GetGroup();
	if (!pGroup)
		return 0;

	uint32 count = 0;
	for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
	{
		if (Player* pMember = itr->getSource())
		{
			if (pMember->getAttackers().empty())
				continue;

			for (auto eachAttacker : pMember->getAttackers())
			{
				if (eachAttacker &&
					eachAttacker->IsAlive() &&
					IsValidHostileTarget(eachAttacker) &&
					me->IsWithinDist(eachAttacker, 100.0f))
					count++;
			}
		}
	}

	return count;
}

bool CombatBotBaseAI::IsValidHostileTarget(Unit const* pTarget, const bool IgnoreCCmark) const
{
	if (!pTarget)
		return false;

	if (!IgnoreCCmark)
		if (const auto pAI = dynamic_cast<PartyBotAI*>(me->AI_NYCTERMOON()))
			for (const auto mark : pAI->m_marksToCC)
				if (Unit* pMark = pAI->GetMarkedTarget(mark))
					if (pMark == pTarget)
						return false;

	if (pTarget->IsFlying() && (m_role == ROLE_TANK || m_role == ROLE_MELEE_DPS))
	{
		const float ground_Z = pTarget->GetMap()->GetHeight(pTarget->GetPositionX(), pTarget->GetPositionY(), pTarget->GetPositionZ(), true);
		const float current_Z = pTarget->GetPositionZ();
		const float Z_diff = current_Z - ground_Z;
		if (Z_diff > me->GetMeleeReach())
			return false;
	}

	// Don't try to attack targets that are far away when I'm rooted
	if (m_role == ROLE_TANK || m_role == ROLE_MELEE_DPS)
	{
		if (const auto pAI = dynamic_cast<PartyBotAI*>(me->AI_NYCTERMOON()))
		{
			Unit* FocusMark = nullptr;
			if (!pAI->m_marksToFocus.empty() && me->GetGroup())
			{
				for (auto const& markId : pAI->m_marksToFocus)
				{
					ObjectGuid targetGuid = me->GetGroup()->GetTargetWithIcon(markId);
					if (targetGuid && targetGuid.IsUnit())
						FocusMark = me->GetMap()->GetUnit(targetGuid);
				}
			}

			if (FocusMark != pTarget && (me->hasUnitState(UNIT_STAT_ROOT) || me->HasAuraType(SPELL_AURA_MOD_ROOT)) && !me->CanReachWithMeleeAttack(pTarget))
				return false;
		}
	}

	return me->IsValidAttackTarget(pTarget) &&
		!pTarget->IsDead() &&
		!pTarget->IsTotalImmune() &&
		pTarget->IsVisibleForOrDetect(me, me, false) &&
		(!pTarget->IsCharmed() || pTarget->HasAura(17244)) &&
		!pTarget->HasBreakableByDamageCrowdControlAura();
}

bool CombatBotBaseAI::IsTank(Player* pTarget)
{
	if (!pTarget)
		return false;

	// Exception for warlocks to be considered as tanks in AQ40 for Twin Emperors
	if (pTarget->getClass() == CLASS_WARLOCK && pTarget->GetZoneId() == 3428)
	{
		if (Group* pGroup = pTarget->GetGroup())
		{
			if (const Unit* pBoss = pGroup->GetWorldBossAttacker())
			{
				const std::string Boss = pBoss->GetName();
				if (Boss == "Emperor Vek'lor" || Boss == "Emperor Vek'nilash")
					return true;
			}
		}
	}

	if (const auto pAI = dynamic_cast<PartyBotAI*>(pTarget->AI()))
	{
		if (pAI->m_role == ROLE_TANK)
			return true;
	}
	else
	{
		const uint8 Class = pTarget->getClass();
		if (Class == CLASS_WARRIOR && pTarget->HasAura(71) || (pTarget->HasSpell(12298) || pTarget->HasSpell(12724) || pTarget->HasSpell(12725) || pTarget->HasSpell(12726) || pTarget->HasSpell(12727)) ||
			Class == CLASS_PALADIN && pTarget->HasAura(25780) || (pTarget->HasSpell(20127) || pTarget->HasSpell(20130) || pTarget->HasSpell(20135) || pTarget->HasSpell(20136) || pTarget->HasSpell(20137)) ||
			Class == CLASS_DRUID && pTarget->GetPowerType() == POWER_RAGE ||
			Class == CLASS_SHAMAN && (pTarget->HasSpell(16253) || pTarget->HasSpell(16298) || pTarget->HasSpell(16299) || pTarget->HasSpell(16300) || pTarget->HasSpell(16301)))
			return true;
	}

	return false;
}

uint32 CombatBotBaseAI::GetAuraID(Unit* pmTarget, const std::string& pmSpellName, Unit* pmCaster, const bool IgnorePassive)
{
	if (!pmTarget)
		return 0;

	const std::set<uint32> spellIDSet = sPlayerBotMgr.GetSpellMap()[pmSpellName];
	for (unsigned int spellID : spellIDSet)
	{
		if (IgnorePassive)
			if (IsPassiveSpell(spellID))
				continue;

		if (pmCaster)
		{
			if (pmTarget->HasCasterAura(spellID, static_cast<ObjectGuid>(pmCaster->GetGUID())))
				return spellID;
		}
		else
		{
			if (pmTarget->HasAura(spellID))
				return spellID;
		}
	}

	return 0;
}

uint32 CombatBotBaseAI::GetAuraStack(Unit* pmTarget, const std::string& pmSpellName, Unit* pmCaster)
{
	uint32 auraCount = 0;
	if (!pmTarget)
	{
		return false;
	}
	std::set<uint32> spellIDSet = sPlayerBotMgr.GetSpellMap()[pmSpellName];
	for (unsigned int spellID : spellIDSet)
	{
		if (pmCaster)
		{
			auraCount = pmTarget->GetAuraStack(spellID, pmCaster->GetObjectGuid());
		}
		else
		{
			auraCount = pmTarget->GetAuraStack(spellID);
		}
		if (auraCount > 0)
		{
			break;
		}
	}

	return auraCount;
}

uint32 CombatBotBaseAI::GetAuraDuration(Unit* pmTarget, const std::string& pmSpellName, Unit* pmCaster)
{
	if (!pmTarget)
	{
		return false;
	}
	uint32 duration = 0;
	const std::set<uint32> spellIDSet = sPlayerBotMgr.GetSpellMap()[pmSpellName];
	for (unsigned int spellID : spellIDSet)
	{
		if (pmCaster)
		{
			duration = pmTarget->GetAuraDuration(spellID, pmCaster->GetObjectGuid());
		}
		else
		{
			duration = pmTarget->GetAuraDuration(spellID);
		}
		if (duration > 0)
		{
			break;
		}
	}

	return duration;
}

bool CombatBotBaseAI::HasAura(Unit* pTarget, Unit* pCaster, const std::string& pSpellName, const bool IgnorePassive) const
{
	if (!pTarget)
		return false;

	const std::set<uint32> spellIDSet = sPlayerBotMgr.GetSpellMap()[pSpellName];
	for (unsigned int spellID : spellIDSet)
	{
		if (IgnorePassive)
			if (IsPositiveSpell(spellID, pCaster, pTarget))
				continue;

		if (pCaster)
		{
			if (pTarget->HasCasterAura(spellID, pCaster->GetObjectGuid()))
				return true;
		}
		else
		{
			if (pTarget->HasAura(spellID))
				return true;
		}
	}

	return false;
}

uint32 CombatBotBaseAI::FindSpellID(const std::string& pmSpellName)
{
	uint32 highestLevel = 0;
	uint32 SpellID = 0;
	const auto pSpell = sPlayerBotMgr.GetSpellMap().find(pmSpellName);
	for (const auto& ID : pSpell->second)
	{
		const SpellEntry* pSpellEntry = sSpellMgr.GetSpellEntry(ID);
		if (pSpellEntry->baseLevel > highestLevel)
		{
			highestLevel = pSpellEntry->baseLevel;
			SpellID = pSpellEntry->Id;
		}
	}
	return SpellID;
}

bool CombatBotBaseAI::CanTryToCastSpell(Unit* pTarget, SpellEntry const* pSpellEntry, const bool CheckHasAura) const
{
	if (!pTarget || !pSpellEntry)
		return false;

	// Check Cooldown
	if (!me->IsSpellReady(pSpellEntry->Id))
		return false;

	// Check Global Cooldown
	if (me->HasGCD(pSpellEntry))
		return false;

	// Check Blacklist
	if (!m_spellBlacklist.empty())
		for (const auto& itr : m_spellBlacklist)
			if (itr == pSpellEntry->SpellName[0])
				return false;

	// Check Range
	const auto spell = new Spell(me, pSpellEntry, TRIGGERED_NONE, me->GetObjectGuid());
	if (spell->CheckRange(false, pTarget) != SPELL_CAST_OK)
		return false;

	// Check Cost
	uint32 const powerCost = Spell::CalculatePowerCost(pSpellEntry, me);
	auto const powerType = static_cast<Powers>(pSpellEntry->powerType);
	if (powerType == POWER_HEALTH)
	{
		if (me->GetHealth() <= powerCost)
			return false;
		return true;
	}
	if (me->GetPower(powerType) < powerCost && !HasAura(me, me, "Clearcasting", true))
		return false;

	// Check Level
	if (pTarget->IsFriendlyTo(me) && IsPositiveSpell(pSpellEntry, me, pTarget) && pTarget->GetLevel() + 10 < pSpellEntry->spellLevel)
	{
		const SpellEntry* pSpellEntryRetry = sSpellMgr.SelectAuraRankForLevel(pSpellEntry, pTarget->GetLevel());
		if (!pSpellEntryRetry || pTarget->GetLevel() + 10 < pSpellEntryRetry->spellLevel)
		{
			std::string Spell_Name;
			if (std::strcmp(pSpellEntry->SpellName[0], "Arcane Brilliance") == 0)
				Spell_Name = "Arcane Intellect";
			else if (std::strcmp(pSpellEntry->SpellName[0], "Gift of the Wild") == 0)
				Spell_Name = "Mark of the Wild";
			else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Fortitude") == 0)
				Spell_Name = "Power Word: Fortitude";
			else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Shadow Protection") == 0)
				Spell_Name = "Shadow Protection";
			else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Spirit") == 0)
				Spell_Name = "Divine Spirit";
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Blessing of Kings") == 0)
				Spell_Name = "Blessing of Kings";
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Blessing of Might") == 0)
				Spell_Name = "Blessing of Might";
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Blessing of Wisdom") == 0)
				Spell_Name = "Blessing of Wisdom";
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Blessing of Salvation") == 0)
				Spell_Name = "Blessing of Salvation";
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Blessing of Sanctuary") == 0)
				Spell_Name = "Blessing of Sanctuary";
			else if (std::strcmp(pSpellEntry->SpellName[0], "Greater Blessing of Light") == 0)
				Spell_Name = "Blessing of Light";

			if (Spell_Name.empty())
				return false;

			const uint32 spell_ID = FindSpellID(Spell_Name);
			if (!spell_ID)
				return false;

			pSpellEntryRetry = sSpellMgr.GetSpellEntry(spell_ID);
			if (!pSpellEntryRetry)
				return false;

			pSpellEntryRetry = sSpellMgr.SelectAuraRankForLevel(pSpellEntryRetry, pTarget->GetLevel());
			if (!pSpellEntryRetry)
				return false;
		}
	}

	// Check if spell can only be use outdoors
	if (pSpellEntry->Attributes & SPELL_ATTR_ONLY_OUTDOORS &&
		!me->GetTerrain()->IsOutdoors(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ()))
		return false;

	if (pSpellEntry->TargetAuraState &&
		!pTarget->HasAuraState(static_cast<AuraState>(pSpellEntry->TargetAuraState)))
		return false;

	if (pSpellEntry->CasterAuraState &&
		!me->HasAuraState(static_cast<AuraState>(pSpellEntry->CasterAuraState)))
		return false;

	if (pTarget->IsImmuneToSpell(pSpellEntry, false))
		return false;

	if (CheckHasAura)
		if (IsSpellAppliesAura(pSpellEntry) && pTarget->HasAura(pSpellEntry->Id))
			return false;

	return true;
}

SpellCastResult CombatBotBaseAI::DoCastSpell(Unit* pTarget, SpellEntry const* pSpellEntry, const bool triggered)
{
	if (!pTarget || !pSpellEntry)
		return SPELL_FAILED_ERROR;

	if (me->IsMounted())
		me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

	TriggerCastFlags trigger = TRIGGERED_NONE;
	if (triggered)
		trigger = TRIGGERED_INSTANT_CAST;

	me->SetTargetGuid(pTarget->GetObjectGuid());
	auto result = me->CastSpell(pTarget, pSpellEntry, TRIGGERED_NONE);

	if ((result == SPELL_FAILED_NEED_AMMO_POUCH || result == SPELL_FAILED_ITEM_NOT_READY) && pSpellEntry->Reagent[0])
	{
		me->StoreNewItemInBestSlots(pSpellEntry->Reagent[0], 5);
		result = me->CastSpell(pTarget, pSpellEntry, trigger);
	}

	if (result == SPELL_FAILED_NOT_SHAPESHIFT)
	{
		switch (me->GetShapeshiftForm())
		{
		case FORM_BEAR:
			me->RemoveAurasDueToSpellByCancel(5487);
			break;
		case FORM_DIREBEAR:
			me->RemoveAurasDueToSpellByCancel(9634);
			break;
		case FORM_CAT:
			me->RemoveAurasDueToSpellByCancel(768);
			break;
		case FORM_MOONKIN:
			me->RemoveAurasDueToSpellByCancel(24858);
			break;
		case FORM_SHADOW:
			me->RemoveAurasDueToSpellByCancel(15473);
			break;
		}

		if (me->GetShapeshiftForm() == FORM_NONE)
			result = me->CastSpell(pTarget, pSpellEntry, trigger);
	}

	if (result == SPELL_FAILED_LOWLEVEL)
	{
		std::string Spell_Name;
		if (std::strcmp(pSpellEntry->SpellName[0], "Arcane Brilliance") == 0)
			Spell_Name = "Arcane Intellect";
		else if (std::strcmp(pSpellEntry->SpellName[0], "Gift of the Wild") == 0)
			Spell_Name = "Mark of the Wild";
		else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Fortitude") == 0)
			Spell_Name = "Power Word: Fortitude";
		else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Shadow Protection") == 0)
			Spell_Name = "Shadow Protection";
		else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Spirit") == 0)
			Spell_Name = "Divine Spirit";
		else
		{
			if (const SpellEntry* pSpellEntryRetry = sSpellMgr.SelectAuraRankForLevel(pSpellEntry, pTarget->GetLevel()))
				result = me->CastSpell(pTarget, pSpellEntryRetry, trigger);
		}

		if (!Spell_Name.empty())
			if (const uint32 spell_ID = FindSpellID(Spell_Name))
				if (const SpellEntry* pSpellEntryRetry = sSpellMgr.GetSpellEntry(spell_ID))
					result = me->CastSpell(pTarget, sSpellMgr.SelectAuraRankForLevel(pSpellEntryRetry, pTarget->GetLevel()), trigger);
	}

	if (m_pDebug)
	{
		if (!m_pDebug->GetObjectGuid() || !ObjectAccessor::FindPlayerNotInWorld(m_pDebug->GetObjectGuid()))
		{
			m_pDebug = nullptr;
			return result;
		}

		if (result == SPELL_CAST_OK)
		{
			const std::string rank = pSpellEntry->Rank[0];
			std::string chatResponse = ChatHandler(me).FormatSpell(pSpellEntry);
			chatResponse += " " + rank + " : " + ChatHandler(me).playerLink(pTarget->GetName());
			me->MonsterWhisper(chatResponse.c_str(), m_pDebug);
		}

		if (m_pDebug->GetSession()->GetSecurity() >= SEC_ADMINISTRATOR)
		{
			if (result != SPELL_CAST_OK)
			{
				std::string chatResponse = "Failed: ";
				chatResponse += ChatHandler(me).FormatSpell(pSpellEntry) + " " + pSpellEntry->Rank[0] + " : " + ChatHandler(me).playerLink(pTarget->GetName()) + " Code: " + std::to_string(result);
				me->MonsterWhisper(chatResponse.c_str(), m_pDebug);
			}
		}
	}

	return result;
}

SpellCastResult CombatBotBaseAI::DoCastSpell(Unit* pTarget, const std::string& SpellName, const bool triggered)
{
	if (!pTarget || SpellName.empty())
		return SPELL_FAILED_ERROR;

	if (me->IsMounted())
		me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

	const uint32 spellID = FindSpellID(SpellName);
	if (!spellID)
		return SPELL_FAILED_ERROR;
	const SpellEntry* pSpellEntry = sSpellMgr.GetSpellEntry(spellID);
	if (!pSpellEntry)
		return SPELL_FAILED_ERROR;

	TriggerCastFlags trigger = TRIGGERED_NONE;
	if (triggered)
		trigger = TRIGGERED_INSTANT_CAST;

	me->SetTargetGuid(pTarget->GetObjectGuid());
	auto result = me->CastSpell(pTarget, pSpellEntry, TRIGGERED_NONE);

	if ((result == SPELL_FAILED_NEED_AMMO_POUCH || result == SPELL_FAILED_ITEM_NOT_READY) && pSpellEntry->Reagent[0])
	{
		me->StoreNewItemInBestSlots(pSpellEntry->Reagent[0], 5);
		result = me->CastSpell(pTarget, pSpellEntry, trigger);
	}

	if (result == SPELL_FAILED_NOT_SHAPESHIFT)
	{
		switch (me->GetShapeshiftForm())
		{
		case FORM_BEAR:
			me->RemoveAurasDueToSpellByCancel(5487);
			break;
		case FORM_DIREBEAR:
			me->RemoveAurasDueToSpellByCancel(9634);
			break;
		case FORM_CAT:
			me->RemoveAurasDueToSpellByCancel(768);
			break;
		case FORM_MOONKIN:
			me->RemoveAurasDueToSpellByCancel(24858);
			break;
		case FORM_SHADOW:
			me->RemoveAurasDueToSpellByCancel(15473);
			break;
		}

		if (me->GetShapeshiftForm() == FORM_NONE)
			result = me->CastSpell(pTarget, pSpellEntry, trigger);
	}

	if (result == SPELL_FAILED_LOWLEVEL)
	{
		std::string Spell_Name;
		if (std::strcmp(pSpellEntry->SpellName[0], "Arcane Brilliance") == 0)
			Spell_Name = "Arcane Intellect";
		else if (std::strcmp(pSpellEntry->SpellName[0], "Gift of the Wild") == 0)
			Spell_Name = "Mark of the Wild";
		else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Fortitude") == 0)
			Spell_Name = "Power Word: Fortitude";
		else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Shadow Protection") == 0)
			Spell_Name = "Shadow Protection";
		else if (std::strcmp(pSpellEntry->SpellName[0], "Prayer of Spirit") == 0)
			Spell_Name = "Divine Spirit";
		else
		{
			if (const SpellEntry* pSpellEntryRetry = sSpellMgr.SelectAuraRankForLevel(pSpellEntry, pTarget->GetLevel()))
				result = me->CastSpell(pTarget, pSpellEntryRetry, trigger);
		}

		if (!Spell_Name.empty())
			if (const uint32 spell_ID = FindSpellID(Spell_Name))
				if (const SpellEntry* pSpellEntryRetry = sSpellMgr.GetSpellEntry(spell_ID))
					result = me->CastSpell(pTarget, sSpellMgr.SelectAuraRankForLevel(pSpellEntryRetry, pTarget->GetLevel()), trigger);
	}

	if (m_pDebug)
	{
		if (!m_pDebug->GetObjectGuid() || !ObjectAccessor::FindPlayerNotInWorld(m_pDebug->GetObjectGuid()))
		{
			m_pDebug = nullptr;
			return result;
		}

		if (result == SPELL_CAST_OK)
		{
			const std::string rank = pSpellEntry->Rank[0];
			std::string chatResponse = ChatHandler(me).FormatSpell(pSpellEntry);
			chatResponse += " " + rank + " : " + ChatHandler(me).playerLink(pTarget->GetName());
			me->MonsterWhisper(chatResponse.c_str(), m_pDebug);
		}

		if (m_pDebug->GetSession()->GetSecurity() >= SEC_ADMINISTRATOR)
		{
			if (result != SPELL_CAST_OK)
			{
				std::string chatResponse = "Failed: ";
				chatResponse += ChatHandler(me).FormatSpell(pSpellEntry) + " " + pSpellEntry->Rank[0] + " : " + ChatHandler(me).playerLink(pTarget->GetName()) + " Code: " + std::to_string(result);
				me->MonsterWhisper(chatResponse.c_str(), m_pDebug);
			}
		}
	}

	return result;
}

bool CombatBotBaseAI::AreOthersOnSameTarget(const ObjectGuid guid, const bool checkMelee, const bool checkSpells, const std::string& Type) const
{
	// Mutex for targeted spells
	if (Group* pGroup = me->GetGroup())
	{
		for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
		{
			if (Player* pMember = itr->getSource())
			{
				if (pMember == me)
					continue;

				if (pMember->GetTargetGuid() == guid)
				{
					if (checkMelee && pMember->hasUnitState(UNIT_STAT_MELEE_ATTACKING))
						return true;

					if (checkSpells && pMember->IsNonMeleeSpellCasted(false, false, true, true))
						return true;

					if (!Type.empty())
					{
						if (Type == "Interrupt")
						{
							if (pMember->getClass() == CLASS_WARRIOR)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Pummel" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Shield Bash"))
									return true;
							}
							else if (pMember->getClass() == CLASS_PALADIN)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Hammer of Justice")
									return true;
							}
							else if (pMember->getClass() == CLASS_ROGUE)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Kick")
									return true;
							}
							else if (pMember->getClass() == CLASS_PRIEST)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Silence")
									return true;
							}
							else if (pMember->getClass() == CLASS_SHAMAN)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Earth Shock")
									return true;
							}
							else if (pMember->getClass() == CLASS_MAGE)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Counterspell")
									return true;
							}
						}
						else if (Type == "Circuit Breaker")
						{
							if (pMember->getClass() == CLASS_PALADIN)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Divine Protection" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Divine Shield" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Blessing of Protection" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Lay on Hands" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Blessing of Sacrifice" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Blessing of Freedom"))
									return true;
							}
							else if (pMember->getClass() == CLASS_HUNTER)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Feign Death")
									return true;
							}
							else if (pMember->getClass() == CLASS_ROGUE)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Vanish")
									return true;
							}
							else if (pMember->getClass() == CLASS_PRIEST)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Power Word: Shield" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Desperate Prayer"))
									return true;
							}
							else if (pMember->getClass() == CLASS_MAGE)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Ice Block")
									return true;
							}
							else if (pMember->getClass() == CLASS_DRUID)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Innervate")
									return true;
							}
						}
						else if (Type == "HealDirect")
						{
							// It's fine to heal on top of off-healers because their heals are weak
							if (const auto pAI = dynamic_cast<PartyBotAI*>(pMember->AI()))
								if (pAI->m_role != ROLE_HEALER)
									return false;

							// Don't take into account players, they're unpredictable
							if (!pMember->AI())
								return false;

							if (pMember->getClass() == CLASS_PRIEST)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Flash Heal" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Heal" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Greater Heal"))
									return true;
							}
							else if (pMember->getClass() == CLASS_PALADIN)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Flash of Light" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Holy Light"))
									return true;
							}
							else if (pMember->getClass() == CLASS_SHAMAN)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Lesser Healing Wave" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Healing Wave" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Chain Heal"))
									return true;
							}
							else if (pMember->getClass() == CLASS_DRUID)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Healing Touch" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Regrowth" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Swiftmend"))
									return true;
							}
						}
						else if (Type == "HealOverTime")
						{
							if (pMember->getClass() == CLASS_PRIEST)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Renew")
									return true;
							}
							else if (pMember->getClass() == CLASS_DRUID)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Rejuvenation")
									return true;
							}
						}
						else if (Type == "Dispel")
						{
							if (pMember->getClass() == CLASS_PALADIN)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Cleanse" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Purify"))
									return true;
							}
							else if (pMember->getClass() == CLASS_PRIEST)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Dispel Magic" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Abolish Disease" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Cure Disease"))
									return true;
							}
							else if (pMember->getClass() == CLASS_MAGE)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Remove Lesser Curse")
									return true;
							}
							else if (pMember->getClass() == CLASS_DRUID)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Remove Curse" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Abolish Disease" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Cure Disease"))
									return true;
							}
							else if (pMember->getClass() == CLASS_SHAMAN)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Cure Disease" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Cure Poison" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Purge"))
									return true;
							}
						}
						else if (Type == "AP Strip")
						{
							if (pMember->getClass() == CLASS_WARRIOR)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Demoralizing Shout")
									return true;
							}
							else if (pMember->getClass() == CLASS_DRUID)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Demoralizing Roar")
									return true;
							}
						}
						else if (Type == "Armor Strip")
						{
							if (pMember->getClass() == CLASS_WARRIOR)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Sunder Armor")
									return true;
							}
							else if (pMember->getClass() == CLASS_DRUID)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Faerie Fire" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Faerie Fire (Feral)"))
									return true;
							}
							else if (pMember->getClass() == CLASS_ROGUE)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Expose Armor")
									return true;
							}
						}
						else if (Type == "Taunt")
						{
							if (pMember->getClass() == CLASS_WARRIOR)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Taunt" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Mocking Blow"))
									return true;
							}
							else if (pMember->getClass() == CLASS_DRUID)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Growl")
									return true;
							}
						}
						else if (Type == "Disarm")
						{
							if (pMember->getClass() == CLASS_WARRIOR)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Disarm")
									return true;
							}
						}
						else if (Type == "Frenzy")
						{
							if (pMember->getClass() == CLASS_HUNTER)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Tranquilizing Shot")
									return true;
							}
						}
						else if (Type == "Curse")
						{
							if (pMember->getClass() == CLASS_WARLOCK)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									(pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Curse of Recklessness" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Curse of the Elements" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Curse of Shadow" || pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Curse of Weakness"))
									return true;
							}
						}
						else if (Type == "Banish")
						{
							if (pMember->getClass() == CLASS_WARLOCK)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Banish")
									return true;
							}
						}
						else if (Type == "Hibernate")
						{
							if (pMember->getClass() == CLASS_DRUID)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Hibernate")
									return true;
							}
						}
						else if (Type == "Polymorph")
						{
							if (pMember->getClass() == CLASS_MAGE)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Polymorph")
									return true;
							}
						}
						else if (Type == "Scorch")
						{
							if (pMember->getClass() == CLASS_MAGE)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Scorch")
									return true;
							}
						}
						else if (Type == "Stun")
						{
							if (pMember->getClass() == CLASS_WARRIOR)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Concussion Blow")
									return true;
							}
							else if (pMember->getClass() == CLASS_PALADIN)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Hammer or Justice")
									return true;
							}
							else if (pMember->getClass() == CLASS_HUNTER)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Intimidation")
									return true;
							}
							else if (pMember->getClass() == CLASS_ROGUE)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Kidney Shot")
									return true;
							}
							else if (pMember->getClass() == CLASS_DRUID)
							{
								if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) && pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->getState() != SPELL_STATE_FINISHED &&
									pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->SpellName[0] == "Bash")
									return true;
							}
						}
					}
				}
			}
		}
	}

	return false;
}

int32 SpellEntry::getDuration() const
{
	SpellDurationEntry const* du;
	du = sSpellDurationStore.LookupEntry(DurationIndex);
	if (!du)
		return 0;
	return (du->Duration[0] == -1) ? -1 : abs(du->Duration[0]);
}

uint32 CombatBotBaseAI::GetHealingDoneBySpell(SpellEntry const* pSpellEntry, Unit* pTarget)
{
	int32 basePoints = 0;
	for (uint32 i = 0; i < MAX_SPELL_EFFECTS; i++)
	{
		switch (pSpellEntry->Effect[i])
		{
			case SPELL_EFFECT_HEAL:
				basePoints += pSpellEntry->EffectBasePoints[i];
				basePoints += pSpellEntry->EffectDieSides[i];
				break;
			case SPELL_EFFECT_APPLY_AURA:
			case SPELL_EFFECT_PERSISTENT_AREA_AURA:
			case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
				if (pSpellEntry->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL)
					basePoints += pSpellEntry->getDuration() / pSpellEntry->EffectAmplitude[i] * pSpellEntry->EffectBasePoints[i];
				break;
			default:
			{
				basePoints += pSpellEntry->EffectBasePoints[i];
				basePoints += pSpellEntry->EffectDieSides[i];
				break;
			}
		}
	}

	int32 BonusHealing;
	int32 BonusHealingReceivedFromEffects;

	if ((pSpellEntry->SpellName[0] == "Flash of Light" ||
		pSpellEntry->SpellName[0] == "Holy Shock")
		&& m_spells.paladin.pHolyLight)
	{
		SpellEntry const* fakeSpell = m_spells.paladin.pHolyLight;
		BonusHealing = static_cast<int32>(me->SpellHealingBonusDone(me, fakeSpell, 0, HEAL));
	}
	else
	{
		BonusHealing = static_cast<int32>(me->SpellHealingBonusDone(me, pSpellEntry, 0, HEAL));
	}

	const int32 PartialHealingDone = basePoints + BonusHealing;
	if ((pSpellEntry->SpellName[0] == "Flash of Light" ||
		pSpellEntry->SpellName[0] == "Holy Shock")
		&& m_spells.paladin.pHolyLight)
	{
		SpellEntry const* fakeSpell = m_spells.paladin.pHolyLight;
		BonusHealingReceivedFromEffects = static_cast<int32>(pTarget->SpellHealingBonusTaken(me, fakeSpell, static_cast<float>(PartialHealingDone), HEAL)) - PartialHealingDone;
	}
	else
	{
		BonusHealingReceivedFromEffects = static_cast<int32>(pTarget->SpellHealingBonusTaken(me, pSpellEntry, static_cast<float>(PartialHealingDone), HEAL)) - PartialHealingDone;
	}

	const int32 TotalHealingDone = basePoints + BonusHealing + BonusHealingReceivedFromEffects;

	return TotalHealingDone;
}