#include "PartyBotAI.h"


float PartyBotAI::MyRage() const
{
	return me->GetPowerPercent(POWER_RAGE);
}

ShapeshiftForm PartyBotAI::MyStance() const
{
	return me->GetShapeshiftForm();
}

float PartyBotAI::RagePoolAmount(const std::string& SpellName) const
{
	// Only Pool for spells that I actively use, not buffs or debuffs which might be cast by someone else
	if (SpellName == "Dump")
		return 70.0f;

	if (SpellName == "Heroic Strike" || SpellName == "Cleave")
		return 50.0f;

	if (SpellName == "Shield Slam")
		return 30.0f;

	return 0.0f;
}

bool PartyBotAI::TacticalMastery() const
{
	if (MyRage() > 25.0f)
		return false;

	// Check Tactical Mastery ranks and Rage levels
	if (me->HasSpell(12677) && MyRage() <= 25.0f ||
		me->HasSpell(12676) && MyRage() <= 20.0f ||
		me->HasSpell(12295) && MyRage() <= 15.0f ||
		MyRage() == 0.0f)
		return true;

	return false;
}

bool PartyBotAI::DumpRage(Unit* pVictim)
{
	if (!pVictim)
		return false;
	//auto me_bot = dynamic_cast<Unit*>(me);
	// Cleave - NO GCD
	if (m_spells.warrior.pCleave)
	{
		if ((!me->GetCurrentSpell(CURRENT_MELEE_SPELL) ||
			me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
			!me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell()) &&
			MyRage() >= RagePoolAmount("Cleave"))
		{
			if (auto spell = new Spell(me, m_spells.warrior.pCleave, false, me->GetObjectGuid()))
			{
				if (spell->CanChainDamage(pVictim, true, true, true) >= 2 &&
					CanTryToCastSpell(pVictim, m_spells.warrior.pCleave) &&
					DoCastSpell(pVictim, m_spells.warrior.pCleave) == SPELL_CAST_OK)
				{
					//return;
				}
			}
		}
	}

	if (m_spells.warrior.pHeroicStrike)
	{
		if ((!me->GetCurrentSpell(CURRENT_MELEE_SPELL) ||
			me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
			!me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell()) &&
			me->CanReachWithMeleeAttack(pVictim) &&
			CanTryToCastSpell(pVictim, m_spells.warrior.pHeroicStrike) &&
			DoCastSpell(pVictim, m_spells.warrior.pHeroicStrike) == SPELL_CAST_OK)
		{
		}
	}

	if (m_spells.warrior.pWhirlwind)
	{
		if (MyStance() == FORM_BERSERKERSTANCE &&
			me->CanReachWithMeleeAttack(pVictim) &&
			me->GetEnemyCountInRadiusAround(me, 13.0f, true, true, true) &&
			me->GetEnemyCountInRadiusAround(me, 8.0f, true, true, true) >= 3 &&
			CanTryToCastSpell(pVictim, m_spells.warrior.pWhirlwind) &&
			DoCastSpell(pVictim, m_spells.warrior.pWhirlwind) == SPELL_CAST_OK)
			return true;
	}

	if (m_spells.warrior.pShieldSlam)
	{
		if (MyStance() == FORM_DEFENSIVESTANCE &&
			me->CanReachWithMeleeAttack(pVictim) &&
			CanTryToCastSpell(pVictim, m_spells.warrior.pShieldSlam) &&
			DoCastSpell(pVictim, m_spells.warrior.pShieldSlam) == SPELL_CAST_OK)
			return true;
	}

	if (m_spells.warrior.pBloodthirst)
	{
		if (me->CanReachWithMeleeAttack(pVictim) &&
			me->IsSpellReady(m_spells.warrior.pBloodthirst->Id) && !me->HasGCD(m_spells.warrior.pBloodthirst) && MyRage() >= 30.0f &&
			DoCastSpell(pVictim, m_spells.warrior.pBloodthirst) == SPELL_CAST_OK)
			return true;
	}

	if (m_spells.warrior.pMortalStrike)
	{
		if (me->CanReachWithMeleeAttack(pVictim) &&
			me->IsSpellReady(m_spells.warrior.pMortalStrike->Id) && !me->HasGCD(m_spells.warrior.pMortalStrike) && MyRage() >= 30.0f &&
			DoCastSpell(pVictim, m_spells.warrior.pMortalStrike) == SPELL_CAST_OK)
			return true;
	}

	if (m_spells.warrior.pWhirlwind)
	{
		if (MyStance() == FORM_BERSERKERSTANCE &&
			me->CanReachWithMeleeAttack(pVictim) &&
			me->GetEnemyCountInRadiusAround(me, 13.0f, true, true, true) &&
			CanTryToCastSpell(pVictim, m_spells.warrior.pWhirlwind) &&
			DoCastSpell(pVictim, m_spells.warrior.pWhirlwind) == SPELL_CAST_OK)
			return true;
	}

	if (m_spells.warrior.pHamstring)
	{
		if (m_role != ROLE_TANK &&
			(MyStance() == FORM_BATTLESTANCE || MyStance() == FORM_BERSERKERSTANCE) &&
			me->CanReachWithMeleeAttack(pVictim) &&
			!me->HasGCD(m_spells.warrior.pHamstring) && MyRage() >= 10.0f &&
			DoCastSpell(pVictim, m_spells.warrior.pHamstring) == SPELL_CAST_OK)
			return true;
	}

	if (m_spells.warrior.pSunderArmor)
	{
		if (m_role == ROLE_TANK &&
			me->CanReachWithMeleeAttack(pVictim) &&
			CanTryToCastSpell(pVictim, m_spells.warrior.pSunderArmor, false) &&
			DoCastSpell(pVictim, m_spells.warrior.pSunderArmor) == SPELL_CAST_OK)
			return true;
	}

	if (m_spells.warrior.pBattleShout)
	{
		if (m_role == ROLE_TANK && m_allow_aoe &&
			!me->CanReachWithMeleeAttack(pVictim) &&
			CanTryToCastSpell(pVictim, m_spells.warrior.pBattleShout, false) &&
			DoCastSpell(pVictim, m_spells.warrior.pBattleShout) == SPELL_CAST_OK)
			return true;
	}

	return false;
}

void PartyBotAI::UpdateOutOfCombat_Warrior()
{
	// Enter Battle Stance by default - NO GCD
	if (m_spells.warrior.pBattleStance)
	{
		if (MyStance() != FORM_BATTLESTANCE && TacticalMastery() &&
			CanTryToCastSpell(me, m_spells.warrior.pBattleStance) &&
			DoCastSpell(me, m_spells.warrior.pBattleStance, true) == SPELL_CAST_OK)
		{
			//return;
		}
	}

	// Pre combat prep
	if (Unit* pVictim = me->GetVictim())
	{
		// Charge
		if (m_spells.warrior.pCharge)
		{
			if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != TIMED_RANDOM_MOTION_TYPE &&
				CanTryToCastSpell(pVictim, m_spells.warrior.pCharge))
			{
				// Enter Battle Stance - NO GCD
				if (m_spells.warrior.pBattleStance)
				{
					if (MyStance() != FORM_BATTLESTANCE && TacticalMastery() &&
						CanTryToCastSpell(me, m_spells.warrior.pBattleStance) &&
						DoCastSpell(me, m_spells.warrior.pBattleStance, true) == SPELL_CAST_OK)
					{
						//return;
					}
				}

				if (MyStance() == FORM_BATTLESTANCE)
				{
					if (DoCastSpell(pVictim, m_spells.warrior.pCharge, true) == SPELL_CAST_OK)
					{
						Emote(COMPANIONS_EMOTE_CHARGE,GetPartyLeader());
						return;
					}
				}
			}
		}

		//Cast Intercept if out of combat and for some reason I couldn't cast Charge
		if (m_spells.warrior.pIntercept)
		{
			if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != TIMED_RANDOM_MOTION_TYPE &&
				CanTryToCastSpell(pVictim, m_spells.warrior.pIntercept))
			{
				// Enter Berserker Stance
				if (m_spells.warrior.pBerserkerStance)
				{
					if (MyStance() != FORM_BERSERKERSTANCE && TacticalMastery() &&
						CanTryToCastSpell(me, m_spells.warrior.pBerserkerStance) &&
						DoCastSpell(me, m_spells.warrior.pBerserkerStance, true) == SPELL_CAST_OK)
					{
						//return;
					}
				}

				if (MyStance() == FORM_BERSERKERSTANCE)
				{
					if (DoCastSpell(pVictim, m_spells.warrior.pIntercept, true) == SPELL_CAST_OK)
					{
						Emote(COMPANIONS_EMOTE_CHARGE,GetPartyLeader());
						return;
					}
				}
			}
		}

		UpdateInCombat_Warrior();
	}

}

void PartyBotAI::UpdateInCombat_Warrior()
{
	if (Unit* pVictim = me->GetVictim())
	{
		// DPS role needs this to prevent getting stuck in place when the target moves away
		if ((me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE ||
			me->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE) &&
			me->CanReachWithMeleeAttack(pVictim))
			me->GetMotionMaster()->MoveChase(pVictim);

		// Cast Intercept in combat
		if (m_spells.warrior.pIntercept)
		{
			if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != TIMED_RANDOM_MOTION_TYPE &&
				CanTryToCastSpell(pVictim, m_spells.warrior.pIntercept))
			{
				// Enter Berserker Stance - NO GCD
				if (m_spells.warrior.pBerserkerStance)
				{
					if (MyStance() != FORM_BERSERKERSTANCE && TacticalMastery() &&
						CanTryToCastSpell(me, m_spells.warrior.pBerserkerStance) &&
						DoCastSpell(me, m_spells.warrior.pBerserkerStance, true) == SPELL_CAST_OK)
					{
						// Heroic Strike and Cleave need to be interrupted if I've dumped enough rage
						if (me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
							me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell())
							me->InterruptSpell(CURRENT_MELEE_SPELL, false);
					}
				}

				if (MyStance() == FORM_BERSERKERSTANCE)
				{
					if (DoCastSpell(pVictim, m_spells.warrior.pIntercept, true) == SPELL_CAST_OK)
					{
						Emote(COMPANIONS_EMOTE_CHARGE,GetPartyLeader());
						return;
					}
				}
			}
		}

		switch (m_role)
		{
			case ROLE_TANK:
			{
				// Circuit Breaker #1
				if (m_spells.warrior.pShieldWall)
				{
					if (MyStance() == FORM_DEFENSIVESTANCE &&
						me->GetHealthPercent() < 25.0f &&
						!me->getAttackers().empty() &&
						CanTryToCastSpell(me, m_spells.warrior.pShieldWall) &&
						DoCastSpell(me, m_spells.warrior.pShieldWall) == SPELL_CAST_OK)
						return;
				}

				// Circuit Breaker #2 - NO GCD
				if (m_spells.warrior.pLastStand)
				{
					if (me->GetHealthPercent() < 25.0f &&
						!me->getAttackers().empty() &&
						CanTryToCastSpell(me, m_spells.warrior.pLastStand) &&
						DoCastSpell(me, m_spells.warrior.pLastStand, true) == SPELL_CAST_OK)
					{
						//return;
					}
				}

				// Circuit Breaker #3
				if (m_spells.warrior.pIntimidatingShout)
				{
					if (!pVictim->hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL) &&
						!pVictim->IsImmuneToMechanic(MECHANIC_FEAR) &&
						me->GetHealthPercent() < 25.0f &&
						!me->getAttackers().empty() &&
						me->GetEnemyCountInRadiusAround(me, 13.0f, true, true, true) &&
						me->GetEnemyCountInRadiusAround(me, 8.0f, true, true, true) >= 3 &&
						CanTryToCastSpell(pVictim, m_spells.warrior.pIntimidatingShout) &&
						DoCastSpell(pVictim, m_spells.warrior.pIntimidatingShout) == SPELL_CAST_OK)
						return;
				}

				// Use execute on bosses even as a tank
				if (m_spells.warrior.pExecute)
				{
					if (IsBoss(pVictim) && pVictim->GetHealthPercent() < 20.0f && me->CanReachWithMeleeAttack(pVictim))
					{
						if (CanTryToCastSpell(pVictim, m_spells.warrior.pExecute))
						{
							// Enter Battle Stance - NO GCD
							if (m_spells.warrior.pBattleStance)
							{
								if (MyStance() != FORM_BATTLESTANCE &&
									CanTryToCastSpell(me, m_spells.warrior.pBattleStance))
								{
									// Dump rage to switch stance
									if (!TacticalMastery())
										if (DumpRage(pVictim))
											return;

									if (TacticalMastery() &&
										DoCastSpell(me, m_spells.warrior.pBattleStance, true) == SPELL_CAST_OK)
									{
										// Heroic Strike and Cleave need to be interrupted if I've dumped enough rage
										if (me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
											me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell())
											me->InterruptSpell(CURRENT_MELEE_SPELL, false);
									}
								}
							}

							if (MyStance() == FORM_BATTLESTANCE)
								DoCastSpell(pVictim, m_spells.warrior.pExecute);
						}
						return;
					}
				}

				// Use Berserker Rage for extra rage mitigation
				if (m_spells.warrior.pBerserkerRage)
				{
					if (!me->getAttackers().empty() && me->getAttackers().size() <= 3 &&
						CanTryToCastSpell(me, m_spells.warrior.pBerserkerRage))
					{
						// Enter Berserker Stance - NO GCD
						if (m_spells.warrior.pBerserkerStance)
						{
							if (MyStance() != FORM_BERSERKERSTANCE && TacticalMastery() &&
								CanTryToCastSpell(me, m_spells.warrior.pBerserkerStance) &&
								DoCastSpell(me, m_spells.warrior.pBerserkerStance, true) == SPELL_CAST_OK)
							{
								// Heroic Strike and Cleave need to be interrupted if I've dumped enough rage
								if (me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
									me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell())
									me->InterruptSpell(CURRENT_MELEE_SPELL, false);
							}
						}

						// Only proceed if in Berserker Stance
						if (MyStance() == FORM_BERSERKERSTANCE)
						{
							if (DoCastSpell(me, m_spells.warrior.pBerserkerRage) == SPELL_CAST_OK)
								return;
						}
					}
				}

				// AoE Taunt
				if (m_spells.warrior.pChallengingShout)
				{
					if (pVictim->GetVictim() && pVictim->GetVictim()->IsPlayer())
					{
						if (Player* pVictimsVictim = pVictim->GetVictim()->ToPlayer())
						{
							if (!IsTank(pVictimsVictim))
							{
								if (me->GetEnemyCountInRadiusAround(me, 15.0f, true, true, true) &&
									me->GetEnemyCountInRadiusAround(me, 10.0f, true, true, true, true) >= 5 &&
									CanTryToCastSpell(pVictim, m_spells.warrior.pChallengingShout) &&
									DoCastSpell(pVictim, m_spells.warrior.pChallengingShout) == SPELL_CAST_OK)
									return;
							}
						}
					}
				}

				// Taunt target if its attacking someone else
				if (m_spells.warrior.pMockingBlow && m_spells.warrior.pTaunt)
				{
					if (pVictim->CanHaveThreatList() && pVictim->GetVictim() && pVictim->GetVictim()->IsPlayer())
					{
						if (Player* pVictimsVictim = pVictim->GetVictim()->ToPlayer())
						{
							if (!IsTank(pVictimsVictim) || IsFocusMarked(pVictim, me) && !IsFocusMarked(pVictim, pVictimsVictim) || (pVictimsVictim->getClass() == CLASS_PALADIN || pVictimsVictim->getClass() == CLASS_SHAMAN) && pVictimsVictim->AI_NYCTERMOON())
							{
								if (pVictimsVictim != me && pVictim->IsInCombat() &&
									!me->IsSpellReady(m_spells.warrior.pTaunt->Id) &&
									!AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "Taunt") &&
									CanTryToCastSpell(pVictim, m_spells.warrior.pMockingBlow))
								{
									// Enter Battle Stance - NO GCD
									if (m_spells.warrior.pBattleStance)
									{
										if (MyStance() != FORM_BATTLESTANCE &&
											(MyRage() >= 10.0f && TacticalMastery() ||
												MyRage() >= 0.0f && TacticalMastery() && m_spells.warrior.pBloodrage && CanTryToCastSpell(me, m_spells.warrior.pBloodrage)) &&
											CanTryToCastSpell(me, m_spells.warrior.pBattleStance) &&
											DoCastSpell(me, m_spells.warrior.pBattleStance, true) == SPELL_CAST_OK)
										{
											// Heroic Strike and Cleave need to be interrupted if I've dumped enough rage
											if (me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
												me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell())
												me->InterruptSpell(CURRENT_MELEE_SPELL, false);
										}
									}

									// Only proceed if in Battle Stance
									if (MyStance() == FORM_BATTLESTANCE)
									{
										// Use Bloodrage if it's precisely enough - NO GCD
										if (m_spells.warrior.pBloodrage)
										{
											if (MyRage() < 10.0f &&
												CanTryToCastSpell(me, m_spells.warrior.pBloodrage) &&
												DoCastSpell(me, m_spells.warrior.pBloodrage, true) == SPELL_CAST_OK)
											{
												//return;
											}
										}

										// Use Mocking Blow whenever is possible
										if (DoCastSpell(pVictim, m_spells.warrior.pMockingBlow) == SPELL_CAST_OK)
											return;
									}
								}
							}
						}
					}
				}

				// Thunderclap Logic
				if (m_spells.warrior.pThunderClap)
				{
					if (me->CanReachWithMeleeAttack(pVictim) &&
						me->GetEnemyCountInRadiusAround(me, 13.0f, true, true, true) &&
						(me->GetEnemyCountInRadiusAround(me, 8.0f, true, true, true) >= 3 || IsBoss(pVictim)) &&
						!HasAura(pVictim,me, "Thunderfury",false) &&
						!HasAura(pVictim, me, "Thunderfury", false) &&
						me->IsSpellReady(m_spells.warrior.pThunderClap->Id) &&
						!me->HasGCD(m_spells.warrior.pThunderClap) &&
						!AreOthersOnSameTarget(me->GetObjectGuid(),false,true))
					{
						// Enter Battle Stance - NO GCD
						if (m_spells.warrior.pBattleStance)
						{
							if (MyStance() != FORM_BATTLESTANCE &&
								(MyRage() >= 20.0f && TacticalMastery() ||
									MyRage() >= 10.0f && TacticalMastery() && m_spells.warrior.pBloodrage && CanTryToCastSpell(me, m_spells.warrior.pBloodrage)) &&
								CanTryToCastSpell(me, m_spells.warrior.pBattleStance) &&
								DoCastSpell(me, m_spells.warrior.pBattleStance, true) == SPELL_CAST_OK)
							{
								// Heroic Strike and Cleave need to be interrupted if I've dumped enough rage
								if (me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
									me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell())
									me->InterruptSpell(CURRENT_MELEE_SPELL, false);
							}
						}

						// Only proceed if in Battle Stance
						if (MyStance() == FORM_BATTLESTANCE)
						{
							// Use Bloodrage if it's precisely enough - NO GCD
							if (m_spells.warrior.pBloodrage)
							{
								if (MyRage() < 20.0f && MyRage() >= 10.0f &&
									CanTryToCastSpell(me, m_spells.warrior.pBloodrage) &&
									DoCastSpell(me, m_spells.warrior.pBloodrage, true) == SPELL_CAST_OK)
								{
									//return;
								}
							}

							// Use Thunderclap whenever it becomes possible
							if (CanTryToCastSpell(pVictim, m_spells.warrior.pThunderClap) &&
								DoCastSpell(pVictim, m_spells.warrior.pThunderClap) == SPELL_CAST_OK)
								return;
						}
					}
				}

				// Enter Defensive Stance - NO GCD
				if (m_spells.warrior.pDefensiveStance)
				{
					if (MyStance() != FORM_DEFENSIVESTANCE &&
						me->CanReachWithMeleeAttack(pVictim) &&
						CanTryToCastSpell(me, m_spells.warrior.pDefensiveStance))
					{
						// Dump rage to switch stance
						if (!TacticalMastery())
							if (DumpRage(pVictim))
								return;

						if (TacticalMastery() &&
							DoCastSpell(me, m_spells.warrior.pDefensiveStance, true) == SPELL_CAST_OK)
						{
							// Heroic Strike and Cleave need to be interrupted if I've dumped enough rage
							if (me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
								me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell())
								me->InterruptSpell(CURRENT_MELEE_SPELL, false);
						}
					}
				}

				// Defensive Stance rotation
				if (MyStance() == FORM_DEFENSIVESTANCE || !m_spells.warrior.pDefensiveStance)
				{
					// Greater Stoneshield Potion
					if (me->IsTier > T0D)
					{
						if (me->IsInCombat() && !HasAura(me,me, "Greater Stoneshield",false))
						{
							if (!me->HasItemCount(13455, 1))
								me->StoreNewItemInBestSlots(13455, 5);

							Item* pItem = GetItemInInventory(13455);
							if (pItem && !pItem->IsInTrade() && UseItem(pItem, me))
								return;
						}
					}

					// Cleave - NO GCD
					if (m_spells.warrior.pCleave)
					{
						if ((!me->GetCurrentSpell(CURRENT_MELEE_SPELL) ||
							me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
							!me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell()) &&
							MyRage() >= RagePoolAmount("Cleave"))
						{
							if (auto spell = new Spell(me, m_spells.warrior.pCleave, false, me->GetObjectGuid()))
							{
								if (spell->CanChainDamage(pVictim, true, true, true) >= 2 &&
									CanTryToCastSpell(pVictim, m_spells.warrior.pCleave) &&
									DoCastSpell(pVictim, m_spells.warrior.pCleave) == SPELL_CAST_OK)
								{
									//return;
								}
							}
						}
					}

					// Heroic Strike - NO GCD
					if (m_spells.warrior.pHeroicStrike)
					{
						if ((!me->GetCurrentSpell(CURRENT_MELEE_SPELL) ||
							me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
							!me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell()) &&
							me->CanReachWithMeleeAttack(pVictim) &&
							MyRage() >= RagePoolAmount("Heroic Strike") &&
							CanTryToCastSpell(pVictim, m_spells.warrior.pHeroicStrike) &&
							DoCastSpell(pVictim, m_spells.warrior.pHeroicStrike) == SPELL_CAST_OK)
						{
							//return;
						}
					}

					// Use Bloodrage whenever I'm low on rage but still healthy enough - NO GCD
					if (m_spells.warrior.pBloodrage)
					{
						if (me->CanReachWithMeleeAttack(pVictim) &&
							MyRage() < 30.0f && me->GetHealthPercent() > 20.0f &&
							CanTryToCastSpell(me, m_spells.warrior.pBloodrage) &&
							DoCastSpell(me, m_spells.warrior.pBloodrage, true) == SPELL_CAST_OK)
						{
							//return;
						}
					}

					// Taunt target if is attacking someone else - NO GCD
					if (m_spells.warrior.pTaunt)
					{
						if (pVictim->CanHaveThreatList() && pVictim->GetVictim() && pVictim->GetVictim()->IsPlayer())
						{
							if (Player* pVictimsVictim = pVictim->GetVictim()->ToPlayer())
							{
								if (pVictimsVictim != me && pVictim->IsInCombat() &&
									(!IsTank(pVictimsVictim) || IsFocusMarked(pVictim, me) && !IsFocusMarked(pVictim, pVictimsVictim) || (pVictimsVictim->getClass() == CLASS_PALADIN || pVictimsVictim->getClass() == CLASS_SHAMAN) && pVictimsVictim->AI_NYCTERMOON()) &&
									!AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "Taunt") &&
									CanTryToCastSpell(pVictim, m_spells.warrior.pTaunt) &&
									DoCastSpell(pVictim, m_spells.warrior.pTaunt, true) == SPELL_CAST_OK)
								{
									//return;
								}
							}
						}
					}

					// Stun off fleeing mobs, targets that are attacking party members or if I'm low hp - NO GCD
					if (m_spells.warrior.pConcussionBlow)
					{
						if (pVictim->GetVictim() && pVictim->GetVictim()->IsPlayer())
						{
							if (Player* pVictimsVictim = pVictim->GetVictim()->ToPlayer())
							{
								if (!pVictim->hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL) &&
									!pVictim->IsImmuneToMechanic(MECHANIC_STUN) &&
									!IsTank(pVictimsVictim) && pVictim->IsInCombat() &&
									me->CanReachWithMeleeAttack(pVictim) &&
									(pVictim->GetMotionMaster()->GetCurrentMovementGeneratorType() == FLEEING_MOTION_TYPE || pVictim->GetMotionMaster()->GetCurrentMovementGeneratorType() == TIMED_FLEEING_MOTION_TYPE ||
										(pVictim->GetVictim() != me || pVictim->GetVictim() == me && me->GetHealthPercent() < 50.0f && pVictim->GetHealthPercent() > 20.0f)) &&
									CanTryToCastSpell(pVictim, m_spells.warrior.pConcussionBlow) &&
									DoCastSpell(pVictim, m_spells.warrior.pConcussionBlow, true) == SPELL_CAST_OK)
								{
									//return;
								}
							}
						}
					}

					// Use Disarm against specific Bosses
					if (m_spells.warrior.pDisarm)
					{
						/*
						 *    BASE_ATTACK   = 0,                                      //< Main-hand weapon
							  OFF_ATTACK    = 1,                                      //< Off-hand weapon
							  RANGED_ATTACK = 2                                       //< Ranged weapon, bow/wand etc.
						 *
						 */
						if ((IsBoss(pVictim) || pVictim->GetEntry() == 11350) &&
							pVictim->IsCreature() && pVictim->ToCreature()->hasWeapon(BASE_ATTACK) &&
							!pVictim->IsImmuneToMechanic(MECHANIC_DISARM) &&
							me->CanReachWithMeleeAttack(pVictim) &&
							!AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "Disarm") &&
							CanTryToCastSpell(pVictim, m_spells.warrior.pDisarm) &&
							DoCastSpell(pVictim, m_spells.warrior.pDisarm) == SPELL_CAST_OK)
							return;
					}

					// Interrupt spellcasters
					if (m_spells.warrior.pShieldBash)
					{
						if (pVictim->IsInterruptable() &&
							me->CanReachWithMeleeAttack(pVictim) &&
							!AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "Interrupt") &&
							CanTryToCastSpell(pVictim, m_spells.warrior.pShieldBash) &&
							DoCastSpell(pVictim, m_spells.warrior.pShieldBash) == SPELL_CAST_OK)
							return;
					}

					// Interrupt spellcasters - secondary targets
					if (m_spells.warrior.pShieldBash)
					{
						const auto SilenceAttacker = SelectAttackerToSilence(m_spells.warrior.pShieldBash);
						if (SilenceAttacker && DoCastSpell(SilenceAttacker, m_spells.warrior.pShieldBash) == SPELL_CAST_OK)
							return;
					}

					// Keep Demoralizing Shout up on victims - Priority for aoe
					if (m_spells.warrior.pDemoralizingShout)
					{
						if (!HasAura(pVictim,me, "Demoralizing Roar",false) &&
							me->GetEnemyCountInRadiusAround(me, 15.0f, true, true, true) &&
							me->GetEnemyCountInRadiusAround(me, 10.0f, true, true, true) >= 3 &&
							!AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "AP Strip") &&
							CanTryToCastSpell(pVictim, m_spells.warrior.pDemoralizingShout) &&
							DoCastSpell(pVictim, m_spells.warrior.pDemoralizingShout) == SPELL_CAST_OK)
							return;
					}

					// Apply and maintain 5 stacks of Sunder Armor
					if (m_spells.warrior.pSunderArmor)
					{
						if (me->CanReachWithMeleeAttack(pVictim) &&
							!HasAura(pVictim,me, "Expose Armor",false) &&
							(!HasAura(pVictim,me, "Sunder Armor",false) && (pVictim->GetArmor() > 0 && (pVictim->GetHealthPercent() > 50.0f || IsBoss(pVictim))) ||
								GetAuraStack(pVictim, "Sunder Armor") < 5 && (pVictim->GetArmor() > 0 && (pVictim->GetHealthPercent() > 50.0f || IsBoss(pVictim))) ||
								HasAura(pVictim,me, "Sunder Armor",false) && GetAuraDuration(pVictim, "Sunder Armor") < 10 * IN_MILLISECONDS && !AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "Armor Strip")) &&
							CanTryToCastSpell(pVictim, m_spells.warrior.pSunderArmor, false) &&
							DoCastSpell(pVictim, m_spells.warrior.pSunderArmor) == SPELL_CAST_OK)
							return;
					}

					// Keep Demoralizing Shout up on victims
					if (m_spells.warrior.pDemoralizingShout)
					{
						if (!HasAura(pVictim,me, "Demoralizing Roar",false) &&
							me->GetEnemyCountInRadiusAround(me, 15.0f, true, true, true) &&
							!AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "AP Strip") &&
							CanTryToCastSpell(pVictim, m_spells.warrior.pDemoralizingShout) &&
							DoCastSpell(pVictim, m_spells.warrior.pDemoralizingShout) == SPELL_CAST_OK)
							return;
					}

					// Keep Battle Shout up
					if (m_spells.warrior.pBattleShout)
					{
						if (m_allow_aoe &&
							!AreOthersOnSameTarget(me->GetObjectGuid(), false, true,"Battle Shout") &&
							CanTryToCastSpell(me, m_spells.warrior.pBattleShout) &&
							DoCastSpell(me, m_spells.warrior.pBattleShout) == SPELL_CAST_OK)
							return;
					}

					// Use Shield Block on cooldown - NO GCD
					if (m_spells.warrior.pShieldBlock)
					{
						if (pVictim->GetVictim() == me &&
							(pVictim->getAttackTimer(BASE_ATTACK) != 0 ||
								pVictim->getAttackTimer(RANGED_ATTACK) != 0) &&
							!pVictim->IsNonMeleeSpellCasted(false, false, true) &&
							CanTryToCastSpell(me, m_spells.warrior.pShieldBlock) &&
							DoCastSpell(me, m_spells.warrior.pShieldBlock, true) == SPELL_CAST_OK)
						{
							//return;
						}
					}

					// Use Shield Slam on cooldown
					if (m_spells.warrior.pShieldSlam)
					{
						if (me->CanReachWithMeleeAttack(pVictim) &&
							MyRage() >= RagePoolAmount("Shield Slam") &&
							CanTryToCastSpell(pVictim, m_spells.warrior.pShieldSlam) &&
							DoCastSpell(pVictim, m_spells.warrior.pShieldSlam) == SPELL_CAST_OK)
							return;
					}

					// Use Revenge on cooldown
					if (m_spells.warrior.pRevenge)
					{
						if (me->CanReachWithMeleeAttack(pVictim) &&
							CanTryToCastSpell(pVictim, m_spells.warrior.pRevenge) &&
							DoCastSpell(pVictim, m_spells.warrior.pRevenge) == SPELL_CAST_OK)
							return;
					}

					// Use Rend against healthy victims
					if (m_spells.warrior.pRend)
					{
						if (me->GetLevel() < 60 && IsBoss(pVictim) && !HasAura(pVictim,me, "Rend", me) &&
							me->CanReachWithMeleeAttack(pVictim) &&
							CanTryToCastSpell(pVictim, m_spells.warrior.pRend, false))
						{
							uint32 damage = 0;
							damage += m_spells.warrior.pRend->getDuration() / m_spells.warrior.pRend->EffectAmplitude[0] * m_spells.warrior.pRend->EffectBasePoints[0];
							damage += static_cast<uint32>(me->SpellDamageBonusDone(pVictim, m_spells.warrior.pRend, EFFECT_INDEX_0, DOT));

							if (damage * 3 < pVictim->GetHealth() &&
								DoCastSpell(pVictim, m_spells.warrior.pRend) == SPELL_CAST_OK)
								return;
						}
					}
				}

				// Dump rage with Demoralizing Shout
				if (m_spells.warrior.pDemoralizingShout)
				{
					if (me->GetEnemyCountInRadiusAround(me, 15.0f, true, true, true) &&
						me->GetEnemyCountInRadiusAround(me, 10.0f, true, true, true) >= 5 && (MyRage() >= RagePoolAmount("Dump") || !me->CanReachWithMeleeAttack(pVictim)) &&
						CanTryToCastSpell(me, m_spells.warrior.pDemoralizingShout, false) &&
						DoCastSpell(me, m_spells.warrior.pDemoralizingShout) == SPELL_CAST_OK)
						return;
				}

				// Dump rage with Battle Shout
				if (m_spells.warrior.pBattleShout)
				{
					if (m_allow_aoe &&
						NumberOfGroupAttackers() >= 1 &&
						(MyRage() >= RagePoolAmount("Dump") && me->GetAllyCountInRadiusAround(me, 20.0f, 100.0f, me) >= 5 || !me->CanReachWithMeleeAttack(pVictim)) &&
						CanTryToCastSpell(me, m_spells.warrior.pBattleShout, false) &&
						DoCastSpell(me, m_spells.warrior.pBattleShout) == SPELL_CAST_OK)
						return;
				}

				// Dump rage with Sunder Armor
				if (m_spells.warrior.pSunderArmor)
				{
					if (MyRage() >= RagePoolAmount("Dump") &&
						me->CanReachWithMeleeAttack(pVictim) &&
						CanTryToCastSpell(me, m_spells.warrior.pSunderArmor, false) &&
						DoCastSpell(me, m_spells.warrior.pSunderArmor) == SPELL_CAST_OK)
						return;
				}

				break;
			}
			case ROLE_MELEE_DPS:
			{
				// Position behind the victim
				if (!me->IsStopped() && me->GetMotionMaster()->GetCurrentMovementGeneratorType() != TIMED_RANDOM_MOTION_TYPE &&
					!(me->IsBehindTarget(pVictim) && me->CanReachWithMeleeAttack(pVictim)) && me->CanFreeMove() && pVictim->GetVictim() != me && pVictim->IsInCombat())
				{
					float x, y, z;
					pVictim->GetRelativePositions(-1.0f, 0.0f, 0.0f, x, y, z);
					me->GetMotionMaster()->MovePoint(1, x, y, z, FORCED_MOVEMENT_RUN, true);
					me->SetInFront(pVictim);
				}

				// Circuit Breaker #1
				if (m_spells.warrior.pIntimidatingShout)
				{
					if (!pVictim->hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL) &&
						!pVictim->IsImmuneToMechanic(MECHANIC_FEAR) &&
						me->GetHealthPercent() < 30.0f &&
						!me->getAttackers().empty() &&
						me->GetEnemyCountInRadiusAround(me, 13.0f, true, true, true) &&
						me->GetEnemyCountInRadiusAround(me, 8.0f, true, true, true) >= 3 &&
						CanTryToCastSpell(pVictim, m_spells.warrior.pIntimidatingShout) &&
						DoCastSpell(pVictim, m_spells.warrior.pIntimidatingShout) == SPELL_CAST_OK)
						return;
				}

				// Threat Manager
				if (OverThreat(pVictim, m_threat_threshold))
					return;

				// DPS Rotation
				if (me->CanReachWithMeleeAttack(pVictim))
				{
					// Dual Wield Rotation
					if (me->IsTier >= T1R)
					{
						// Dispel Bloodthirst when reaching the Buff limit
						if (me->GetPositiveAurasCount() >= 30 && HasAura(me,me, "Bloodthirst", true))
						{
							if (const uint32 SpellID = GetAuraID(me, "Bloodthirst", nullptr, true))
								me->RemoveAurasDueToSpellByCancel(SpellID);
						}

						// Berserker Stance Rotation
						if (m_spells.warrior.pBerserkerStance)
						{
							// Enter Berserker Stance - NO GCD
							if (MyStance() != FORM_BERSERKERSTANCE)
							{
								if (CanTryToCastSpell(me, m_spells.warrior.pBerserkerStance))
								{
									// Dump rage to switch stance
									if (!TacticalMastery() && DumpRage(pVictim))
										return;

									if (TacticalMastery() &&
										DoCastSpell(me, m_spells.warrior.pBerserkerStance, true) == SPELL_CAST_OK)
									{
										// Heroic Strike and Cleave need to be interrupted if I've dumped enough rage
										if (me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
											me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell())
											me->InterruptSpell(CURRENT_MELEE_SPELL, false);
									}
								}
							}

							if (MyStance() == FORM_BERSERKERSTANCE)
							{
								// Cleave - NO GCD
								if (m_spells.warrior.pCleave)
								{
									if ((!me->GetCurrentSpell(CURRENT_MELEE_SPELL) ||
										me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
										!me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell()) &&
										(MyRage() >= RagePoolAmount("Cleave") || m_spells.warrior.pBloodthirst && me->GetSpellCooldown(m_spells.warrior.pBloodthirst) >= 4000))
									{
										if (auto spell = new Spell(me, m_spells.warrior.pCleave, false, me->GetObjectGuid()))
										{
											if (spell->CanChainDamage(pVictim, true, true, true) >= 2 &&
												CanTryToCastSpell(pVictim, m_spells.warrior.pCleave) &&
												DoCastSpell(pVictim, m_spells.warrior.pCleave) == SPELL_CAST_OK)
											{
												//return;
											}
										}
									}
								}

								// Heroic Strike - NO GCD
								if (m_spells.warrior.pHeroicStrike)
								{
									if ((!me->GetCurrentSpell(CURRENT_MELEE_SPELL) ||
										me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
										!me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell()) &&
										(MyRage() >= RagePoolAmount("Heroic Strike") || m_spells.warrior.pBloodthirst && me->GetSpellCooldown(m_spells.warrior.pBloodthirst) >= 4000) &&
										CanTryToCastSpell(pVictim, m_spells.warrior.pHeroicStrike) &&
										DoCastSpell(pVictim, m_spells.warrior.pHeroicStrike) == SPELL_CAST_OK)
									{
										//return;
									}
								}

								// Use Bloodrage whenever I'm low on rage but still healthy enough - NO GCD
								if (m_spells.warrior.pBloodrage)
								{
									if (MyRage() < 30.0f && me->GetHealthPercent() > 20.0f &&
										CanTryToCastSpell(me, m_spells.warrior.pBloodrage) &&
										DoCastSpell(me, m_spells.warrior.pBloodrage, true) == SPELL_CAST_OK)
									{
										//return;
									}
								}

								// Interrupt spellcasters
								if (m_spells.warrior.pPummel)
								{
									if (pVictim->IsInterruptable() && !AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "Interrupt") &&
										CanTryToCastSpell(pVictim, m_spells.warrior.pPummel) &&
										DoCastSpell(pVictim, m_spells.warrior.pPummel) == SPELL_CAST_OK)
										return;
								}

								// Interrupt spellcasters - secondary targets
								if (m_spells.warrior.pPummel)
								{
									const auto SilenceAttacker = SelectAttackerToSilence(m_spells.warrior.pPummel);
									if (SilenceAttacker && DoCastSpell(SilenceAttacker, m_spells.warrior.pPummel) == SPELL_CAST_OK)
										return;
								}

								// Apply and maintain 5 stacks of Sunder Armor
								if (m_spells.warrior.pSunderArmor)
								{
									if (me->CanReachWithMeleeAttack(pVictim) &&
										!HasAura(pVictim,me, "Expose Armor",false) &&
										(!HasAura(pVictim,me, "Sunder Armor",false) && (pVictim->GetArmor() > 0 && (pVictim->GetHealthPercent() > 50.0f || IsBoss(pVictim))) ||
											GetAuraStack(pVictim, "Sunder Armor") < 5 && (pVictim->GetArmor() > 0 && (pVictim->GetHealthPercent() > 50.0f || IsBoss(pVictim))) ||
											HasAura(pVictim,me, "Sunder Armor",false) && GetAuraDuration(pVictim, "Sunder Armor") < 10 * IN_MILLISECONDS && !AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "Armor Strip")) &&
										CanTryToCastSpell(pVictim, m_spells.warrior.pSunderArmor, false) &&
										DoCastSpell(pVictim, m_spells.warrior.pSunderArmor) == SPELL_CAST_OK)
										return;
								}

								// Keep Battle Shout up on self
								if (m_spells.warrior.pBattleShout)
								{
									if (m_allow_aoe &&
										!AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "Battle Shout") &&
										CanTryToCastSpell(me, m_spells.warrior.pBattleShout) &&
										DoCastSpell(me, m_spells.warrior.pBattleShout) == SPELL_CAST_OK)
										return;
								}

								// Mighty Rage Potion
								if (me->IsTier > T0D && me->IsInCombat() && MyRage() <= 50.0f &&
									(IsBoss(pVictim) && pVictim->GetHealthPercent() < 35.0f || me->GetEnemyCountInRadiusAround(pVictim, 10.0f) >= 5))
								{
									if (!me->HasItemCount(13442, 1))
										me->StoreNewItemInBestSlots(13442, 5);
									Item* pItem = GetItemInInventory(13442);
									if (pItem && !pItem->IsInTrade() && UseItem(pItem, me))
										return;
								}

								// Use Death Wish against elites
								if (m_spells.warrior.pDeathWish)
								{
									if (me->getAttackers().empty() &&
										IsBoss(pVictim) && pVictim->GetHealthPercent() < 50.0f &&
										CanTryToCastSpell(me, m_spells.warrior.pDeathWish) &&
										DoCastSpell(me, m_spells.warrior.pDeathWish) == SPELL_CAST_OK)
										return;
								}

								// Use Recklessness against bosses
								if (m_spells.warrior.pRecklessness)
								{
									if (me->getAttackers().empty() &&
										IsBoss(pVictim) && pVictim->GetHealthPercent() < 25.0f &&
										CanTryToCastSpell(me, m_spells.warrior.pRecklessness) &&
										DoCastSpell(me, m_spells.warrior.pRecklessness) == SPELL_CAST_OK)
										return;
								}

								// Use Whirlwind if AoE as a priority
								if (m_spells.warrior.pWhirlwind)
								{
									if (me->GetEnemyCountInRadiusAround(me, 13.0f, true, true, true) &&
										me->GetEnemyCountInRadiusAround(me, 8.0f, true, true, true) >= 3 &&
										CanTryToCastSpell(pVictim, m_spells.warrior.pWhirlwind) &&
										DoCastSpell(pVictim, m_spells.warrior.pWhirlwind) == SPELL_CAST_OK)
										return;
								}

								// Execute when the damage isn't enough to kill the target to avoid wasting rage
								if (m_spells.warrior.pExecute)
								{
									if (pVictim->GetHealthPercent() < 20.0f &&
										CanTryToCastSpell(pVictim, m_spells.warrior.pExecute))
									{
										if (!(m_spells.warrior.pBloodthirst && me->GetTotalAttackPowerValue(BASE_ATTACK) >= 2000.0f && CanTryToCastSpell(pVictim, m_spells.warrior.pBloodthirst)))
										{
											auto damage = static_cast<uint32>(me->CalculateSpellEffectValue(pVictim, m_spells.warrior.pExecute, EFFECT_INDEX_0,
												m_spells.warrior.pExecute->EffectBasePoints));
											//damage += dither(static_cast<float>(me->GetPower(POWER_RAGE)) * m_spells.warrior.pExecute->DmgMultiplier[0]);

											if ((damage < pVictim->GetHealth() || IsBoss(pVictim)) &&
												DoCastSpell(pVictim, m_spells.warrior.pExecute) == SPELL_CAST_OK)
												return;
										}
									}
								}

								// Use Bloodthirst on cooldown
								if (m_spells.warrior.pBloodthirst)
								{
									if (CanTryToCastSpell(pVictim, m_spells.warrior.pBloodthirst, false) &&
										DoCastSpell(pVictim, m_spells.warrior.pBloodthirst) == SPELL_CAST_OK)
										return;
								}

								// Use Whirlwind if Bloodthirst is on cooldown
								if (m_spells.warrior.pWhirlwind)
								{
									if (me->GetEnemyCountInRadiusAround(me, 13.0f, true, true, true) &&
										CanTryToCastSpell(pVictim, m_spells.warrior.pWhirlwind))
									{
										if (m_spells.warrior.pBloodthirst)
										{
											if ((MyRage() >= 55 || me->GetSpellCooldown(m_spells.warrior.pBloodthirst) >= 2000) &&
												DoCastSpell(pVictim, m_spells.warrior.pWhirlwind) == SPELL_CAST_OK)
												return;
										}
										else if (DoCastSpell(pVictim, m_spells.warrior.pWhirlwind) == SPELL_CAST_OK)
											return;
									}
								}

								// Dump rage with Hamstring
								if (m_spells.warrior.pHamstring)
								{
									if (MyRage() >= RagePoolAmount("Dump") &&
										((!m_spells.warrior.pBloodthirst || !me->IsSpellReady(m_spells.warrior.pBloodthirst->Id)) &&
											(!m_spells.warrior.pWhirlwind || !me->IsSpellReady(m_spells.warrior.pWhirlwind->Id))) &&
										CanTryToCastSpell(pVictim, m_spells.warrior.pHamstring, false) &&
										DoCastSpell(pVictim, m_spells.warrior.pHamstring) == SPELL_CAST_OK)
										return;
								}
							}
						}
					}
					// Two-Handed Rotation
					else
					{
						// Use Disarm against specific Bosses
						if (m_spells.warrior.pDisarm)
						{
							if (me->IsSpellReady(m_spells.warrior.pDisarm->Id) &&
								!me->HasGCD(m_spells.warrior.pDisarm) &&
								pVictim->GetHealthPercent() > 25.0f &&
								(IsBoss(pVictim) || pVictim->GetEntry() == 11350) &&
								pVictim->IsCreature() && pVictim->ToCreature()->hasWeapon(BASE_ATTACK) &&
								!pVictim->IsImmuneToMechanic(MECHANIC_DISARM) &&
								me->CanReachWithMeleeAttack(pVictim) &&
								!AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "Disarm"))
							{
								// Enter Defensive Stance - NO GCD
								if (m_spells.warrior.pDefensiveStance)
								{
									if (MyStance() != FORM_DEFENSIVESTANCE && TacticalMastery() &&
										CanTryToCastSpell(me, m_spells.warrior.pDefensiveStance) &&
										DoCastSpell(me, m_spells.warrior.pDefensiveStance, true) == SPELL_CAST_OK)
									{
										// Heroic Strike and Cleave need to be interrupted if I've dumped enough rage
										if (me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
											me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell())
											me->InterruptSpell(CURRENT_MELEE_SPELL, false);
									}
								}

								// Only proceed if in Defensive Stance
								if (MyStance() == FORM_DEFENSIVESTANCE)
								{
									// Use Bloodrage if needed - NO GCD
									if (m_spells.warrior.pBloodrage)
									{
										if (MyRage() < 20.0f &&
											CanTryToCastSpell(me, m_spells.warrior.pBloodrage) &&
											DoCastSpell(me, m_spells.warrior.pBloodrage, true) == SPELL_CAST_OK)
										{
											//return;
										}
									}

									// Use Disarm whenever it becomes possible
									if (CanTryToCastSpell(pVictim, m_spells.warrior.pDisarm))
									{
										if (DoCastSpell(pVictim, m_spells.warrior.pDisarm) == SPELL_CAST_OK)
											return;
									}
									else if (MyRage() < 20.0f)
										return;
								}
							}
						}

						// Sweeping Strikes Logic - NO GCD
						if (m_spells.warrior.pSweepingStrikes)
						{
							if (me->GetEnemyCountInRadiusAround(pVictim, 13.0f, true, true, true) &&
								me->GetEnemyCountInRadiusAround(pVictim, 8.0f, true, true, true) >= 3 &&
								me->IsSpellReady(m_spells.warrior.pSweepingStrikes->Id) &&
								!me->HasGCD(m_spells.warrior.pSweepingStrikes))
							{
								// Enter Battle Stance - NO GCD
								if (m_spells.warrior.pBattleStance)
								{
									if (MyStance() != FORM_BATTLESTANCE && TacticalMastery() &&
										CanTryToCastSpell(me, m_spells.warrior.pBattleStance) &&
										DoCastSpell(me, m_spells.warrior.pBattleStance, true) == SPELL_CAST_OK)
									{
										// Heroic Strike and Cleave need to be interrupted if I've dumped enough rage
										if (me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
											me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell())
											me->InterruptSpell(CURRENT_MELEE_SPELL, false);
									}
								}

								// Only proceed if in Battle Stance
								if (MyStance() == FORM_BATTLESTANCE)
								{
									// Use Bloodrage if needed - NO GCD
									if (m_spells.warrior.pBloodrage)
									{
										if (MyRage() < 30.0f &&
											CanTryToCastSpell(me, m_spells.warrior.pBloodrage) &&
											DoCastSpell(me, m_spells.warrior.pBloodrage, true) == SPELL_CAST_OK)
										{
											//return;
										}
									}

									// Use Sweeping Strikes whenever it becomes possible - NO GCD
									if (CanTryToCastSpell(pVictim, m_spells.warrior.pSweepingStrikes))
									{
										if (DoCastSpell(pVictim, m_spells.warrior.pSweepingStrikes, true) == SPELL_CAST_OK)
										{
											//return;
										}
									}
									else if (MyRage() < 30.0f)
										return;
								}
							}
						}

						// Overpower Logic
						if (m_spells.warrior.pOverpower)
						{
							if (me->CanReachWithMeleeAttack(pVictim) &&
								pVictim->GetObjectGuid() == me->GetReactiveTarget(REACTIVE_OVERPOWER) &&
								me->IsSpellReady(m_spells.warrior.pOverpower->Id) && !me->HasGCD(m_spells.warrior.pOverpower))
							{
								// Enter Battle Stance - NO GCD
								if (m_spells.warrior.pBattleStance)
								{
									if (MyStance() != FORM_BATTLESTANCE)
									{
										if (me->GetReactiveTimer(REACTIVE_OVERPOWER) >= 1500 && // There's at least 1000 delay between casting this and casting Overpower
											TacticalMastery() && CanTryToCastSpell(me, m_spells.warrior.pBattleStance) &&
											DoCastSpell(me, m_spells.warrior.pBattleStance, true) == SPELL_CAST_OK)
										{
											// Heroic Strike and Cleave need to be interrupted if I've dumped enough rage
											if (me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
												me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell())
												me->InterruptSpell(CURRENT_MELEE_SPELL, false);
										}

										if (me->GetReactiveTimer(REACTIVE_OVERPOWER) >= 3500) // Takes over 2000 to check after dump + 1000 to stance dance
										{
											// Dump rage to switch stance
											if (!TacticalMastery() && DumpRage(pVictim))
												return;
										}
									}
								}

								// Only proceed if in Battle Stance
								if (MyStance() == FORM_BATTLESTANCE)
								{
									// Use Overpower whenever it becomes possible
									if (CanTryToCastSpell(pVictim, m_spells.warrior.pOverpower))
									{
										if (DoCastSpell(pVictim, m_spells.warrior.pOverpower) == SPELL_CAST_OK)
											return;
									}
									else if (MyRage() < 5.0f)
										return;
								}
							}
						}

						// Use Thunderclap until I get Sweeping Strikes
						if (m_spells.warrior.pThunderClap && !m_spells.warrior.pSweepingStrikes)
						{
							if (me->CanReachWithMeleeAttack(pVictim) &&
								me->GetEnemyCountInRadiusAround(me, 13.0f, true, true, true) &&
								me->GetEnemyCountInRadiusAround(me, 8.0f, true, true, true) >= 3 &&
								!HasAura(pVictim,me, "Thunder Clap",false) &&
								me->IsSpellReady(m_spells.warrior.pThunderClap->Id) &&
								!me->HasGCD(m_spells.warrior.pThunderClap) &&
								!AreOthersOnSameTarget(me->GetObjectGuid(), false, true,"Thunder Clap"))
							{
								// Enter Battle Stance - NO GCD
								if (m_spells.warrior.pBattleStance)
								{
									if (MyStance() != FORM_BATTLESTANCE &&
										(MyRage() >= 20.0f && TacticalMastery() ||
											MyRage() >= 10.0f && TacticalMastery() && m_spells.warrior.pBloodrage && CanTryToCastSpell(me, m_spells.warrior.pBloodrage)) &&
										CanTryToCastSpell(me, m_spells.warrior.pBattleStance) &&
										DoCastSpell(me, m_spells.warrior.pBattleStance, true) == SPELL_CAST_OK)
									{
										// Heroic Strike and Cleave need to be interrupted if I've dumped enough rage
										if (me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
											me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell())
											me->InterruptSpell(CURRENT_MELEE_SPELL, false);
									}
								}

								// Only proceed if in Battle Stance
								if (MyStance() == FORM_BATTLESTANCE)
								{
									// Use Bloodrage if it's precisely enough - NO GCD
									if (m_spells.warrior.pBloodrage)
									{
										if (MyRage() < 20.0f && MyRage() >= 10.0f &&
											CanTryToCastSpell(me, m_spells.warrior.pBloodrage) &&
											DoCastSpell(me, m_spells.warrior.pBloodrage, true) == SPELL_CAST_OK)
										{
											//return;
										}
									}

									// Use Thunderclap whenever it becomes possible
									if (CanTryToCastSpell(pVictim, m_spells.warrior.pThunderClap))
									{
										if (DoCastSpell(pVictim, m_spells.warrior.pThunderClap) == SPELL_CAST_OK)
											return;
									}
									else if (MyRage() < 20.0f)
										return;
								}
							}
						}

						// Use Rend against healthy victims
						if (m_spells.warrior.pRend)
						{
							if (me->GetLevel() < 60 && IsBoss(pVictim) && !HasAura(pVictim,me, "Rend", me) &&
								(MyStance() == FORM_BATTLESTANCE || MyStance() == FORM_DEFENSIVESTANCE) &&
								CanTryToCastSpell(pVictim, m_spells.warrior.pRend, false))
							{
								uint32 damage = 0;
								damage += m_spells.warrior.pRend->getDuration() / m_spells.warrior.pRend->EffectAmplitude[0] * m_spells.warrior.pRend->EffectBasePoints[0];
								damage += static_cast<uint32>(me->SpellDamageBonusDone(pVictim, m_spells.warrior.pRend, EFFECT_INDEX_0, DOT));

								if (damage * 3 < pVictim->GetHealth() &&
									DoCastSpell(pVictim, m_spells.warrior.pRend) == SPELL_CAST_OK)
									return;
							}
						}

						// Enter Berserker Stance if possible - NO GCD
						if (m_spells.warrior.pBerserkerStance)
						{
							// Enter Berserker Stance - NO GCD
							if (MyStance() != FORM_BERSERKERSTANCE)
							{
								if ((!m_spells.warrior.pOverpower || (pVictim->GetObjectGuid() != me->GetReactiveTarget(REACTIVE_OVERPOWER) || !me->IsSpellReady(m_spells.warrior.pOverpower->Id))) &&
									CanTryToCastSpell(me, m_spells.warrior.pBerserkerStance))
								{
									// Dump rage to switch stance
									if (!TacticalMastery() && DumpRage(pVictim))
										return;

									if (TacticalMastery() &&
										DoCastSpell(me, m_spells.warrior.pBerserkerStance, true) == SPELL_CAST_OK)
									{
										// Heroic Strike and Cleave need to be interrupted if I've dumped enough rage
										if (me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
											me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell())
											me->InterruptSpell(CURRENT_MELEE_SPELL, false);
									}
								}
							}
						}

						// Cleave - NO GCD
						if (m_spells.warrior.pCleave)
						{
							if ((!me->GetCurrentSpell(CURRENT_MELEE_SPELL) ||
								me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
								!me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell()) &&
								MyRage() >= RagePoolAmount("Cleave"))
							{
								if (auto spell = new Spell(me, m_spells.warrior.pCleave, false, me->GetObjectGuid()))
								{
									if (spell->CanChainDamage(pVictim, true, true, true) >= 2 &&
										CanTryToCastSpell(pVictim, m_spells.warrior.pCleave) &&
										DoCastSpell(pVictim, m_spells.warrior.pCleave) == SPELL_CAST_OK)
									{
										//return;
									}
								}
							}
						}

						// Heroic Strike - NO GCD
						if (m_spells.warrior.pHeroicStrike)
						{
							if ((!me->GetCurrentSpell(CURRENT_MELEE_SPELL) ||
								me->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
								!me->GetCurrentSpell(CURRENT_MELEE_SPELL)->m_spellInfo->IsNextMeleeSwingSpell()) &&
								MyRage() >= RagePoolAmount("Heroic Strike") &&
								CanTryToCastSpell(pVictim, m_spells.warrior.pHeroicStrike) &&
								DoCastSpell(pVictim, m_spells.warrior.pHeroicStrike) == SPELL_CAST_OK)
							{
								//return;
							}
						}

						// Mighty Rage Potion
						if (MyStance() == FORM_BERSERKERSTANCE && me->IsTier > T0D && me->IsInCombat() && MyRage() <= 25.0f &&
							(IsBoss(pVictim) && pVictim->GetHealthPercent() < 25.0f || me->GetEnemyCountInRadiusAround(pVictim, 10.0f) >= 5))
						{
							if (!me->HasItemCount(13442, 1))
								me->StoreNewItemInBestSlots(13442, 5);
							Item* pItem = GetItemInInventory(13442);
							if (pItem && !pItem->IsInTrade() && UseItem(pItem, me))
								return;
						}

						// Use Bloodrage whenever I'm low on rage but still healthy enough - NO GCD
						if (m_spells.warrior.pBloodrage)
						{
							// If I have Berserker Stance then I need to be in it before I use Bloodrage
							if (m_spells.warrior.pBerserkerStance)
							{
								if (MyStance() == FORM_BERSERKERSTANCE)
								{
									if (MyRage() < 30.0f && me->GetHealthPercent() > 20.0f &&
										CanTryToCastSpell(me, m_spells.warrior.pBloodrage) &&
										DoCastSpell(me, m_spells.warrior.pBloodrage, true) == SPELL_CAST_OK)
									{
										//return;
									}
								}
							}
							else
							{
								if (MyRage() < 30.0f && me->GetHealthPercent() > 20.0f &&
									CanTryToCastSpell(me, m_spells.warrior.pBloodrage) &&
									DoCastSpell(me, m_spells.warrior.pBloodrage, true) == SPELL_CAST_OK)
								{
									//return;
								}
							}
						}

						// AoE slow for kiting
						if (m_spells.warrior.pPiercingHowl)
						{
							if (me->GetEnemyCountInRadiusAround(me, 15.0f, true, true, true))
							{
								if (Group* pGroup = me->GetGroup())
								{
									for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
									{
										if (Player* pMember = itr->getSource())
										{
											if (pMember->getAttackers().empty())
												continue;

											if (IsTank(pMember))
												continue;

											for (auto eachAttacker : pMember->getAttackers())
											{
												if (eachAttacker &&
													eachAttacker->IsMoving() &&
													IsValidHostileTarget(eachAttacker) &&
													me->GetCombatDistance(eachAttacker) < 10.0f &&
													me->IsWithinLOSInMap(eachAttacker) &&
													!eachAttacker->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) &&
													CanTryToCastSpell(eachAttacker, m_spells.warrior.pPiercingHowl) &&
													DoCastSpell(eachAttacker, m_spells.warrior.pPiercingHowl) == SPELL_CAST_OK)
													return;
											}
										}
									}
								}
							}
						}

						// Interrupt spellcasters
						if (m_spells.warrior.pPummel)
						{
							if (MyStance() == FORM_BERSERKERSTANCE &&
								pVictim->IsInterruptable() && !AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "Interrupt") &&
								CanTryToCastSpell(pVictim, m_spells.warrior.pPummel) &&
								DoCastSpell(pVictim, m_spells.warrior.pPummel) == SPELL_CAST_OK)
								return;
						}

						// Interrupt spellcasters - secondary targets
						if (m_spells.warrior.pPummel)
						{
							if (MyStance() == FORM_BERSERKERSTANCE)
							{
								const auto SilenceAttacker = SelectAttackerToSilence(m_spells.warrior.pPummel);
								if (SilenceAttacker && DoCastSpell(SilenceAttacker, m_spells.warrior.pPummel) == SPELL_CAST_OK)
									return;
							}
						}

						// Use Demoralizing Shout if surrounded
						if (m_spells.warrior.pDemoralizingShout)
						{
							if (!HasAura(pVictim,me,"Demoralizing Roar",false) &&
								me->GetEnemyCountInRadiusAround(me, 15.0f, true, true, true) &&
								me->GetEnemyCountInRadiusAround(me, 10.0f, true, true, true) >= 3 &&
								!AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "AP Strip") &&
								CanTryToCastSpell(pVictim, m_spells.warrior.pDemoralizingShout) &&
								DoCastSpell(pVictim, m_spells.warrior.pDemoralizingShout) == SPELL_CAST_OK)
								return;
						}

						// Apply and maintain 5 stacks of Sunder Armor
						if (m_spells.warrior.pSunderArmor)
						{
							if (me->CanReachWithMeleeAttack(pVictim) &&
								!HasAura(pVictim,me, "Expose Armor",false) &&
								(!HasAura(pVictim,me, "Sunder Armor",false) && (pVictim->GetArmor() > 0 && (pVictim->GetHealthPercent() > 50.0f || IsBoss(pVictim))) ||
									GetAuraStack(pVictim, "Sunder Armor") < 5 && (pVictim->GetArmor() > 0 && (pVictim->GetHealthPercent() > 50.0f || IsBoss(pVictim))) ||
									HasAura(pVictim,me, "Sunder Armor",false) && GetAuraDuration(pVictim, "Sunder Armor") < 10 * IN_MILLISECONDS && !AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "Armor Strip")) &&
								CanTryToCastSpell(pVictim, m_spells.warrior.pSunderArmor, false) &&
								DoCastSpell(pVictim, m_spells.warrior.pSunderArmor) == SPELL_CAST_OK)
								return;
						}

						// Keep Battle Shout up on self
						if (m_spells.warrior.pBattleShout)
						{
							if (m_allow_aoe &&
								!AreOthersOnSameTarget(pVictim->GetObjectGuid(), false, false, "Battle Shout") &&
								CanTryToCastSpell(me, m_spells.warrior.pBattleShout) &&
								DoCastSpell(me, m_spells.warrior.pBattleShout) == SPELL_CAST_OK)
								return;
						}

						// Use Recklessness against bosses
						if (m_spells.warrior.pRecklessness)
						{
							if (MyStance() == FORM_BERSERKERSTANCE &&
								me->getAttackers().empty() &&
								IsBoss(pVictim) && pVictim->GetHealthPercent() < 20.0f &&
								CanTryToCastSpell(me, m_spells.warrior.pRecklessness) &&
								DoCastSpell(me, m_spells.warrior.pRecklessness) == SPELL_CAST_OK)
								return;
						}

						// Use Whirlwind if AoE as a priority
						if (m_spells.warrior.pWhirlwind)
						{
							if (MyStance() == FORM_BERSERKERSTANCE &&
								me->GetEnemyCountInRadiusAround(me, 13.0f, true, true, true) &&
								me->GetEnemyCountInRadiusAround(me, 8.0f, true, true, true) >= 3 &&
								CanTryToCastSpell(pVictim, m_spells.warrior.pWhirlwind) &&
								DoCastSpell(pVictim, m_spells.warrior.pWhirlwind) == SPELL_CAST_OK)
								return;
						}

						// Execute when the damage isn't enough to kill the target to avoid wasting rage
						if (m_spells.warrior.pExecute)
						{ 
							if (pVictim->GetHealthPercent() < 20.0f &&
								CanTryToCastSpell(pVictim, m_spells.warrior.pExecute))
							{
								auto damage = static_cast<uint32>(me->CalculateSpellEffectValue(pVictim, m_spells.warrior.pExecute, EFFECT_INDEX_0,
									m_spells.warrior.pExecute->EffectBasePoints));
								//damage += dither(static_cast<float>(me->GetPower(POWER_RAGE)) * m_spells.warrior.pExecute->DmgMultiplier[0]);

								if ((damage < pVictim->GetHealth() || IsBoss(pVictim)) &&
									DoCastSpell(pVictim, m_spells.warrior.pExecute) == SPELL_CAST_OK)
									return;
							}
						}

						// Use Mortal Strike on cooldown
						if (m_spells.warrior.pMortalStrike)
						{
							if (CanTryToCastSpell(pVictim, m_spells.warrior.pMortalStrike, false) &&
								DoCastSpell(pVictim, m_spells.warrior.pMortalStrike) == SPELL_CAST_OK)
								return;
						}

						// Use Whirlwind if Mortal Strike is on cooldown
						if (m_spells.warrior.pWhirlwind)
						{
							if (MyStance() == FORM_BERSERKERSTANCE &&
								me->GetEnemyCountInRadiusAround(me, 13.0f, true, true, true) &&
								CanTryToCastSpell(pVictim, m_spells.warrior.pWhirlwind))
							{
								if (m_spells.warrior.pMortalStrike)
								{
									if (!me->IsSpellReady(m_spells.warrior.pMortalStrike->Id) &&
										DoCastSpell(pVictim, m_spells.warrior.pWhirlwind) == SPELL_CAST_OK)
										return;
								}
								else if (DoCastSpell(pVictim, m_spells.warrior.pWhirlwind) == SPELL_CAST_OK)
									return;
							}
						}

						// Dump rage with Hamstring
						if (m_spells.warrior.pHamstring)
						{
							if (MyRage() >= RagePoolAmount("Dump") &&
								((!m_spells.warrior.pMortalStrike || !me->IsSpellReady(m_spells.warrior.pMortalStrike->Id)) &&
									(!m_spells.warrior.pWhirlwind || !me->IsSpellReady(m_spells.warrior.pWhirlwind->Id))) &&
								CanTryToCastSpell(pVictim, m_spells.warrior.pHamstring, false) &&
								DoCastSpell(pVictim, m_spells.warrior.pHamstring) == SPELL_CAST_OK)
								return;
						}
					}
				}

				break;
			}
		}
	}
}