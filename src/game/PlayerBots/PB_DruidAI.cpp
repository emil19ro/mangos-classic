#include "PartyBotAI.h"

void PartyBotAI::LeaveCombatDruidForm() const
{
	if (me->getClass() != CLASS_DRUID)
		return;

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
	}
}

void PartyBotAI::UpdateOutOfCombat_Druid()
{
}

void PartyBotAI::UpdateInCombat_Druid()
{
}