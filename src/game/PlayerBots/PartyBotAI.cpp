#include "PartyBotAI.h"
#include "PlayerBotMgr.h"
#include "MotionGenerators/PathFinder.h"
#include "World/World.h"

void PartyBotAI::UpdateOutOfCombatAI()
{
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


void PartyBotAI::UpdateAI(uint32 const diff)
{
	
}