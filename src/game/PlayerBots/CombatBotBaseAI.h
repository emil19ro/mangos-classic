#pragma once
#include "PlayerBotAI.h"

class CombatBotBaseAI : public PlayerBotAI
{
public:

	CombatBotBaseAI() : PlayerBotAI(nullptr)
	{
		for (auto& ptr : m_spells.raw.spells)
			ptr = nullptr;
	}

	// Constructor Variables
	union
	{
		struct
		{
			SpellEntry const* spells[50]; // Array size aproximate max spells that a class can have
		} raw;
		struct
		{
			// Warrior Arms Spells
			SpellEntry const* pBattleStance;
			SpellEntry const* pCharge;
			SpellEntry const* pHamstring;
			SpellEntry const* pOverpower;
			SpellEntry const* pRend;
			SpellEntry const* pThunderClap;
			SpellEntry const* pHeroicStrike;
			SpellEntry const* pMockingBlow;
			SpellEntry const* pRetaliation;

			// Warrior Fury Spells
			SpellEntry const* pVictoryRush;
			SpellEntry const* pCommandingShout;
			SpellEntry const* pPummel;
			SpellEntry const* pSlam;
			SpellEntry const* pCleave;
			SpellEntry const* pExecute;
			SpellEntry const* pDemoralizingShout;
			SpellEntry const* pIntercept;
			SpellEntry const* pBattleShout;
			SpellEntry const* pBerserkerRage;
			SpellEntry const* pBerserkerStance;
			SpellEntry const* pChallengingShout;
			SpellEntry const* pIntimidatingShout;
			SpellEntry const* pRecklessness;
			SpellEntry const* pWhirlwind;

			// Warrior Protection Spells
			SpellEntry const* pRevenge;
			SpellEntry const* pShieldBash;
			SpellEntry const* pSunderArmor;
			SpellEntry const* pBloodrage;
			SpellEntry const* pDefensiveStance;
			SpellEntry const* pDisarm;
			SpellEntry const* pIntervene;
			SpellEntry const* pShieldArmor;
			SpellEntry const* pShieldWall;
			SpellEntry const* pSpellReflection;
			SpellEntry const* pTaunt;
			SpellEntry const* pShieldBlock;
			SpellEntry const* pStanceMastery;

			// Warrior Arms Talents
			SpellEntry const* pMortalStrike;
			SpellEntry const* pImprovedRend;
			SpellEntry const* pMaceSpecialization;
			SpellEntry const* pImprovedCharge;
			SpellEntry const* pImprovedOverpower;
			SpellEntry const* pDeepWounds;
			SpellEntry const* pImprovedHamstring;
			SpellEntry const* pIronWill;
			SpellEntry const* pDeflection;
			SpellEntry const* pImpale;
			SpellEntry const* pImprovedHeroicStrike;
			SpellEntry const* pSecondWind;
			SpellEntry const* pPoleaxeSpecialization;
			SpellEntry const* pSwordSpecialization;
			SpellEntry const* pTwoHandedWeaponSpecialization;
			SpellEntry const* pImprovedThunderClap;
			SpellEntry const* pBloodFrenzy;
			SpellEntry const* pImprovedDiscipline;
			SpellEntry const* pImprovedIntercept;
			SpellEntry const* pImprovedMortalStrike;
			SpellEntry const* pAngerManagement;
			SpellEntry const* pDeathWish;
			SpellEntry const* pEndlessRage;
			SpellEntry const* pIronWIll;

			// Warrior Fury Talents
			SpellEntry const* pBloodthirst;
			SpellEntry const* pRampage;
			SpellEntry const* pUnbridledWarth;
			SpellEntry const* pCommandingPresence;
			SpellEntry const* pEnrage;
			SpellEntry const* pCruelty;
			SpellEntry const* pBoomingVoice;
			SpellEntry const* pImprovedCleave;
			SpellEntry const* pFlurry;
			SpellEntry const* pImprovedDemoralizingShout;
			SpellEntry const* pImprovedSlam;
			SpellEntry const* pBloodCraze;
			SpellEntry const* pDuelWieldSpecialization;
			SpellEntry const* pImprovedWhirlwind;
			SpellEntry const* pImprovedBerserkerRage;
			SpellEntry const* pWeaponMastery;
			SpellEntry const* pPrecision;
			SpellEntry const* pSweepingStrikes;
			SpellEntry const* pPiercingHowl;
			SpellEntry const* pImprovedBerserkerStance;
			SpellEntry const* pImprovedExecute;

			// Warrior Protection Talents
			SpellEntry const* pDevastate;
			SpellEntry const* pShieldSlam;
			SpellEntry const* pImprovedDisarm;
			SpellEntry const* pDefiance;
			SpellEntry const* pImprovedSunderArmor;
			SpellEntry const* pImprovedShieldBlock;
			SpellEntry const* pImprovedBloodrage;
			SpellEntry const* pImprovedShieldWall;
			SpellEntry const* pImprovedTaunt;
			SpellEntry const* pImprovedShieldBash;
			SpellEntry const* pAnticipation;
			SpellEntry const* pOneHandedWeaponSpecialization;
			SpellEntry const* pToughness;
			SpellEntry const* pTacticalMastery;
			SpellEntry const* pShieldSpecialization;
			SpellEntry const* pImprovedRevange;
			SpellEntry const* pImprovedDefensiveStance;
			SpellEntry const* pShieldMastery;
			SpellEntry const* pFocusRage;
			SpellEntry const* pVitality;
			SpellEntry const* pLastStand;
			SpellEntry const* pConcussionBlow;
		} warrior;
		struct
		{
			// Paladin Holy Spells
			SpellEntry const* pRedemption;
			SpellEntry const* pConsecration;
			SpellEntry const* pSealOfWisdom;
			SpellEntry const* pFlashOfLight;
			SpellEntry const* pHolyLight;
			SpellEntry const* pTurnEvil;
			SpellEntry const* pHolyWrath;
			SpellEntry const* pLayOnHands;
			SpellEntry const* pBlessingOfLight;
			SpellEntry const* pBlessingOfWisdom;
			SpellEntry const* pExorcism;
			SpellEntry const* pHammerOfWrath;
			SpellEntry const* pSealOfRighteousness;
			SpellEntry const* pSealOfLight;
			SpellEntry const* pTurnUndead;
			SpellEntry const* pGreaterBlessingOfLight;
			SpellEntry const* pGreaterBlessingOfWisdom;
			SpellEntry const* pCleanse;
			SpellEntry const* pPurify;
			SpellEntry const* pSenseOfUndead;
			SpellEntry const* pSummonCharger;

			// Paladin Protection Spells
			SpellEntry const* pDivineShield;
			SpellEntry const* pBlessingOfProtection;
			SpellEntry const* pSpiritualAttunement;
			SpellEntry const* pHammerOfJustice;
			SpellEntry const* pBlessingOfSacrifice;
			SpellEntry const* pDevotionAura;
			SpellEntry const* pDivineProtection;
			SpellEntry const* pFireResistanceAura;
			SpellEntry const* pShadowResistanceAura;
			SpellEntry const* pFrostResistanceAura;
			SpellEntry const* pGreaterBlessingOfSanctuary;
			SpellEntry const* pSealOfJustice;
			SpellEntry const* pBlessingOfFredom;
			SpellEntry const* pBlessingOfSalvation;
			SpellEntry const* pConcentrationAura;
			SpellEntry const* pDivineIntervention;
			SpellEntry const* pGreaterBlessingOfKings;
			SpellEntry const* pGreaterBlessingOfSalvation;
			SpellEntry const* pRighteousDefense;
			SpellEntry const* pRighteousFury;
			SpellEntry const* pAvengersShield;

			// Paladin Retribution Spells
			SpellEntry const* pSealOfBlood;
			SpellEntry const* pSealOfVengence;
			SpellEntry const* pSealOfMartyr;
			SpellEntry const* pSealOfCorruption;
			SpellEntry const* pJudgementOfTheCrusader;
			SpellEntry const* pRetributionAura;
			SpellEntry const* pBloodCorruption;
			SpellEntry const* pJudgementOfCorruption;
			SpellEntry const* pBlessingOfMight;
			SpellEntry const* pSealOfTheCrusader;
			SpellEntry const* pGreaterBlessingOfMight;
			SpellEntry const* pAvangingWrath;
			SpellEntry const* pCrusaderAura;
			SpellEntry const* pJudgement;
			SpellEntry const* pAvengingWrath;
			SpellEntry const* pRepentance;
			SpellEntry const* pSanctityAura;

			// Paladin Holy Talents;
			SpellEntry const* pHolyShock;
			SpellEntry const* pLightsGrace;
			SpellEntry const* pholyGuidance;
			SpellEntry const* pUndyieldingFaith;
			SpellEntry const* pImprovedBlessingOfWisdom;
			SpellEntry const* pHolyPower;
			SpellEntry const* pIllumination;
			SpellEntry const* pImprovedLayOnHands;
			SpellEntry const* pBlessedLife;
			SpellEntry const* pSpiritualFocus;
			SpellEntry const* pPureOfHeart;
			SpellEntry const* pPurifyingPower;
			SpellEntry const* pImprovedSealOfRighteousness;
			SpellEntry const* pHealingLight;
			SpellEntry const* pDivineStrenght;
			SpellEntry const* pDivineIntellect;
			SpellEntry const* pSanctifiedLight;
			SpellEntry const* pAuraMastery;
			SpellEntry const* pDivineFavor;
			SpellEntry const* pDivineIllumination;

			// Paladin Protection Talents
			SpellEntry const* pAvaragesShield;
			SpellEntry const* pArdentDefender;
			SpellEntry const* pBlessingOfSanctuary;
			SpellEntry const* pHolyShield;
			SpellEntry const* pCombatExpertise;
			SpellEntry const* pRedoubt;
			SpellEntry const* pReckoning;
			SpellEntry const* pOneHandedWeaponSpecialization;
			SpellEntry const* pSacredDuty;
			SpellEntry const* pImprovedHolyShield;
			SpellEntry const* pToughness;
			SpellEntry const* pShieldSpecialization;
			SpellEntry const* pImprovedConcentrationAura;
			SpellEntry const* pAnticipation;
			SpellEntry const* pImprovedDevotionAura;
			SpellEntry const* pImprovedRighteousFury;
			SpellEntry const* pImprovedHammerOfJustice;
			SpellEntry const* pSpellWarding;
			SpellEntry const* pGuardiansFavor;
			SpellEntry const* pStoicism;
			SpellEntry const* pBlessingOfKings;

			// Paladin Retribution Talents
			SpellEntry const* pSealOfCommand;
			SpellEntry const* pEyeForAnEye;
			SpellEntry const* pVindication;
			SpellEntry const* pConvinction;
			SpellEntry const* pFanaticism;
			SpellEntry const* pImprovedSealOfTheCrusader;
			SpellEntry const* pImprovedSanctityAura;
			SpellEntry const* pImprovedBlessingOfMight;
			SpellEntry const* pDeflection;
			SpellEntry const* pPorsuitOfJustice;
			SpellEntry const* pCrushade;
			SpellEntry const* pTwoHandedWeaponsSpecialization;
			SpellEntry const* pSanctifiedSeals;
			SpellEntry const* pImprovedJudgement;
			SpellEntry const* pVengence;
			SpellEntry const* pImprovedRetributionAura;
			SpellEntry const* pBenediction;
			SpellEntry const* pDivinePurpose;
			SpellEntry const* pSanctifiedJudgement;
			SpellEntry const* pCrusaderStrike;
			SpellEntry const* pRepetance;
		} paladin;
		struct
		{
			// Hunter BeastMastery Spells
			SpellEntry const* pKillCommand;
			SpellEntry const* pScareBeast;
			SpellEntry const* pMendPet;
			SpellEntry const* pAspectOfTheHawk;
			SpellEntry const* pAspectOfTheWild;
			SpellEntry const* pAspectOfTheBeast;
			SpellEntry const* pAspectOfTheCheetah;
			SpellEntry const* pAspectOfTheMonkey;
			SpellEntry const* pAspectOfThePack;
			SpellEntry const* pAspectOfTheViper;
			SpellEntry const* pBeastLore;
			SpellEntry const* pCallPet;
			SpellEntry const* pDismissPet;
			SpellEntry const* pEagleEye;
			SpellEntry const* pEyeOfTheBeast;
			SpellEntry const* pFeedPet;
			SpellEntry const* pRevivePet;
			SpellEntry const* pTameBeast;

			// Hunter Marksmanship Spells
			SpellEntry const* pSteadyShot;
			SpellEntry const* pHuntersMark;
			SpellEntry const* pMultiShot;
			SpellEntry const* pSerpentSting;
			SpellEntry const* pViperSting;
			SpellEntry const* pArcaneShot;
			SpellEntry const* pVolley;
			SpellEntry const* pDistractingShot;
			SpellEntry const* pAutoShot;
			SpellEntry const* pConcussiveShot;
			SpellEntry const* pFlare;
			SpellEntry const* pRapidFire;
			SpellEntry const* pScorpidSting;
			SpellEntry const* pTranquilizingShot;
			SpellEntry const* pScatterShot;
			SpellEntry const* pSilencingShot;
			SpellEntry const* pTrueshotAura;

			// Hunter Survival Spells
			SpellEntry const* pFreezingTrap;
			SpellEntry const* pRaptorStrike;
			SpellEntry const* pWingClip;
			SpellEntry const* pDisengage;
			SpellEntry const* pImmolationTrap;
			SpellEntry const* pMongooseBite;
			SpellEntry const* pExplosiveTrap;
			SpellEntry const* pFeignDeath;
			SpellEntry const* pFrostTrap;
			SpellEntry const* pMisdirection;
			SpellEntry const* pSnakeTrap;
			SpellEntry const* pTrackBeasts;
			SpellEntry const* pTrackDragonkin;
			SpellEntry const* pTrackElementals;
			SpellEntry const* pTrackDemons;
			SpellEntry const* pTrackGiants;
			SpellEntry const* pTrackHiden;
			SpellEntry const* pTrackHumanoids;
			SpellEntry const* pTrackUndead;

			// Hunter BeastTraining Spells(pet talents)
			SpellEntry const* pGreaterStamina;
			SpellEntry const* pGrowl;
			SpellEntry const* pArcaneResistance;
			SpellEntry const* pFireResistance;
			SpellEntry const* pNatureResistance;
			SpellEntry const* pFrostResistance;
			SpellEntry const* pShadowResistance;
			SpellEntry const* pNatureArmor;

			// Hunter BeastMastery Talents
			SpellEntry const* pAnimalHandler;
			SpellEntry const* pBestialDiscipline;
			SpellEntry const* pBestialSwiftness;
			SpellEntry const* pBestialWrath;
			SpellEntry const* pCatlikeReflexes;
			SpellEntry const* pEnduranceTraining;
			SpellEntry const* pFerociousInspiration;
			SpellEntry const* pFerocity;
			SpellEntry const* pFocusedFire;
			SpellEntry const* pFrenzy;
			SpellEntry const* pImprovedAspectOfTheHawk;
			SpellEntry const* pImprovedAspectOfTheMonkey;
			SpellEntry const* pImprovedMendPet;
			SpellEntry const* pImprovedRevivePet;
			SpellEntry const* pIntimidation;
			SpellEntry const* pPathfinding;
			SpellEntry const* pSerpentsSwiftness;
			SpellEntry const* pSpiritBond;

			// Hunter Marksmanship Talents
			SpellEntry const* pAimedShot;
			SpellEntry const* pBarrage;
			SpellEntry const* pCarefulAim;
			SpellEntry const* pCombatExperience;
			SpellEntry const* pConcussiveBarrage;
			SpellEntry const* pEfficiency;
			SpellEntry const* pGoForTheThroat;
			SpellEntry const* pImprovedArcaneShot;
			SpellEntry const* pImprovedBarrage;
			SpellEntry const* pImprovedConcussiveShot;
			SpellEntry const* pImprovedHuntersMark;
			SpellEntry const* pImprovedStings;
			SpellEntry const* pLethalShots;
			SpellEntry const* pMasterMarksman;

			// Hunter Survival Talents
			SpellEntry const* pCleverTraps;
			SpellEntry const* pCounterattack;
			SpellEntry const* pDeflection;
			SpellEntry const* pDeterrence;
			SpellEntry const* pEntrapment;
			SpellEntry const* pExposeWeakness;
			SpellEntry const* pHawkEye;
			SpellEntry const* pHumanoidSlaying;
			SpellEntry const* pImprovedWingClip;
			SpellEntry const* pKillerInstinct;
			SpellEntry const* pLightningReflexes;
			SpellEntry const* pMasterTactician;
			SpellEntry const* pMonsterSlaying;
			SpellEntry const* pReadiness;
			SpellEntry const* pResourcefulness;
			SpellEntry const* pSavegeStrikes;
			SpellEntry const* pSurefooted;
		} hunter;
		struct
		{
			// Rogue Assasination Spells
			SpellEntry const* pAmbush;
			SpellEntry const* pCheapShot;
			SpellEntry const* pDeadlyThrow;
			SpellEntry const* pEnvenom;
			SpellEntry const* pEviscerate;
			SpellEntry const* pExposeArmor;
			SpellEntry const* pFindWeakness;
			SpellEntry const* pGarrote;
			SpellEntry const* pKidneyShot;
			SpellEntry const* pMutilate;
			SpellEntry const* pRupture;
			SpellEntry const* pSliceAndDice;

			// Weapon Poisons
			SpellEntry const* pMainHandPoison;
			SpellEntry const* pOffHandPoison;

			// Rogue Combat Spells
			SpellEntry const* pBackstab;
			SpellEntry const* pEvasion;
			SpellEntry const* pFeint;
			SpellEntry const* pGouge;
			SpellEntry const* pKick;
			SpellEntry const* pShiv;
			SpellEntry const* pSinisterStrike;
			SpellEntry const* pSprint;
			SpellEntry const* pRiposte;

			// Rogue Subtlety Spells
			SpellEntry const* pVanish;
			SpellEntry const* pStealth;
			SpellEntry const* pSap;
			SpellEntry const* pSafeFall;
			SpellEntry const* pPickPoket;
			SpellEntry const* pDistract;
			SpellEntry const* pDisarmTrap;
			SpellEntry const* pDetectTraps;
			SpellEntry const* pCloakOfShadow;
			SpellEntry const* pBlind;
			SpellEntry const* pGhostlyStrike;
			SpellEntry const* pShadowstep;

			// Rogue Lockpicking and Poisons
			SpellEntry const* pPickLock;
			SpellEntry const* pAnestheticPoison;
			SpellEntry const* pCripplingPoison;
			SpellEntry const* pDeadlyPoison;
			SpellEntry const* pInstantPoison;
			SpellEntry const* pMindNumbingPoison;
			SpellEntry const* pPoisons;
			SpellEntry const* pWoundPoison;

			// Rogue Assassination Talents
			SpellEntry const* pColdBlood;
			SpellEntry const* pDeadenedNerves;
			SpellEntry const* pFleetFooted;
			SpellEntry const* pImprovedEviscerate;
			SpellEntry const* pImprovedExposeArmor;
			SpellEntry const* pImprovedKidneyShot;
			SpellEntry const* pImprovedPoisons;
			SpellEntry const* pLethality;
			SpellEntry const* pMalice;
			SpellEntry const* pMasterPoisoner;
			SpellEntry const* pMurder;
			SpellEntry const* pPuncturingWounds;
			SpellEntry const* pQuickRecovery;
			SpellEntry const* pRelentlessStrikes;
			SpellEntry const* pRemorselessAttack;
			SpellEntry const* pRuthlessness;

			// Rogue Combat Talents
			SpellEntry const* pAdrenalineRush;
			SpellEntry const* pAggresion;
			SpellEntry const* pBladeFlurry;
			SpellEntry const* pBladeTwisting;
			SpellEntry const* pCombatPotency;
			SpellEntry const* pDaggerSpecialization;
			SpellEntry const* pDeflection;
			SpellEntry const* pDuelWieldSpecialization;
			SpellEntry const* pEndurance;
			SpellEntry const* pImprovedGouge;
			SpellEntry const* pImprovedKick;
			SpellEntry const* pImprovedSinisterStrike;
			SpellEntry const* pImprovedSliceAndDice;
			SpellEntry const* pImprovedSprint;
			SpellEntry const* pLightningReflexes;
			SpellEntry const* pMaceSpecialization;

			// Rogue Subtlety Talents
			SpellEntry const* pCamouflage;
			SpellEntry const* pCheatDeath;
			SpellEntry const* pDeadLines;
			SpellEntry const* pDirtyDeeds;
			SpellEntry const* pDirtyTricks;
			SpellEntry const* pElusiveness;
			SpellEntry const* pEnvelopingShadows;
			SpellEntry const* pHemorrhage;
			SpellEntry const* pInitiative;
			SpellEntry const* pMasterOfDeception;
			SpellEntry const* pMasterOfSubtlety;
			SpellEntry const* pOpportunity;
			SpellEntry const* pPremeditation;
			SpellEntry const* pPreparation;
			SpellEntry const* pSerratedBlades;
		} rogue;
		struct
		{
			// Priest Discipline Spells
			SpellEntry const* pConsumeMagic;
			SpellEntry const* pDispelMagic;
			SpellEntry const* pElunesGrace;
			SpellEntry const* pFearWard;
			SpellEntry const* pFeedback;
			SpellEntry const* pInnerFire;
			SpellEntry const* pLevitate;
			SpellEntry const* pManaBurn;
			SpellEntry const* pPowerWordFortitude;
			SpellEntry const* pPowerWordShield;
			SpellEntry const* pPrayerOfFortitude;
			SpellEntry const* pPrayerOfSpirit;
			SpellEntry const* pShackleUndead;
			SpellEntry const* pStarshards;
			SpellEntry const* pSymbolOfHope;
			SpellEntry const* pInnerFocus;
			SpellEntry const* pMassDispel;
			SpellEntry const* pPowerInfusion;

			// Priest Holy Spells
			SpellEntry const* pAbolishDisease;
			SpellEntry const* pBindingHeal;
			SpellEntry const* pChastise;
			SpellEntry const* pCureDisease;
			SpellEntry const* pDesperatePrayer;
			SpellEntry const* pFlashHeal;
			SpellEntry const* pGreaterHeal;
			SpellEntry const* pHeal;
			SpellEntry const* pHolyFire;
			SpellEntry const* pLesserHeal;
			SpellEntry const* pPrayerHealing;
			SpellEntry const* pRenew;
			SpellEntry const* pResurrection;
			SpellEntry const* pSmite;
			SpellEntry const* pPrayerOfMending;

			// Priest Shadow Spells
			SpellEntry const* pDevouringPlague;
			SpellEntry const* pFade;
			SpellEntry const* pHexOfWeakness;
			SpellEntry const* pMindBlast;
			SpellEntry const* pMindSoothe;
			SpellEntry const* pMindVision;
			SpellEntry const* pPrayerOfShadowProtection;
			SpellEntry const* pPsychicScream;
			SpellEntry const* pShadowProtection;
			SpellEntry const* pShadowWordDeath;
			SpellEntry const* pShadowWordPain;
			SpellEntry const* pShadowfiend;
			SpellEntry const* pShadowGuard;
			SpellEntry const* pTouchOfWeakness;
			SpellEntry const* pMindControl;
			SpellEntry const* pShadowform;
			SpellEntry const* pSilence;
			SpellEntry const* pVampiricEmbrace;
			SpellEntry const* pVampiricTouch;

			// Priest Discipline Talents
			SpellEntry const* pAbsolution;
			SpellEntry const* pDivineSpirit;
			SpellEntry const* pEnlightenment;
			SpellEntry const* pFocusedPower;
			SpellEntry const* pFocusedWill;
			SpellEntry const* pForceOfWill;
			SpellEntry const* pImprovedDivineSpirit;
			SpellEntry const* pImprovedInnerFire;
			SpellEntry const* pImprovedManaBurn;
			SpellEntry const* pImprovedPowerWordFortitude;
			SpellEntry const* pImprovedPowerWordShield;
			SpellEntry const* pMartyrdom;
			SpellEntry const* pMeditation;
			SpellEntry const* pMentalAgility;
			SpellEntry const* pMentalStrenght;
			SpellEntry const* pPainSuppression;
			SpellEntry const* pReflectiveShield;

			// Priest Holy Talents
			SpellEntry const* pBlessedRecovery;
			SpellEntry const* pBlessedResilience;
			SpellEntry const* pCircleOfHealing;
			SpellEntry const* pDivineFury;
			SpellEntry const* pEmpoweredHealing;
			SpellEntry const* pHealingFocus;
			SpellEntry const* pHealingPrayers;
			SpellEntry const* pHolyConcentration;
			SpellEntry const* pHolyNova;
			SpellEntry const* pHolyReach;
			SpellEntry const* pHolySpecialization;
			SpellEntry const* pImprovedHealing;
			SpellEntry const* pImprovedRenew;
			SpellEntry const* pInspiration;
			SpellEntry const* pLightWell;
			SpellEntry const* pSpellWarding;
			SpellEntry const* pSpiritOfRedemption;

			// Priest Shadow Talents
			SpellEntry const* pBlackout;
			SpellEntry const* pDarkness;
			SpellEntry const* pFocusedMind;
			SpellEntry const* pImprovedFade;
			SpellEntry const* pImprovedMindBlast;
			SpellEntry const* pImprovedPsychicScream;
			SpellEntry const* pImprovedShadowWordPain;
			SpellEntry const* pImprovedVampiricEmbrace;
			SpellEntry const* pMindFlay;
			SpellEntry const* pMisery;
			SpellEntry const* pShadowAffinity;
			SpellEntry const* pShadowFocus;
			SpellEntry const* pShadowPower;
			SpellEntry const* pShadowReach;
			SpellEntry const* pShadowResilience;
			SpellEntry const* pShadowWeaving;
		} priest;
		struct
		{
			// Shaman Elemental Combat Spells
			SpellEntry const* pChainLightning;
			SpellEntry const* pEarthShock;
			SpellEntry const* pEarthbindTotem;
			SpellEntry const* pFireElementalTotem;
			SpellEntry const* pFireNovaTotem;
			SpellEntry const* pFlameShock;
			SpellEntry const* pFrostShock;
			SpellEntry const* pLightningBolt;
			SpellEntry const* pMagmaTotem;
			SpellEntry const* pPurge;
			SpellEntry const* pSearingTotem;
			SpellEntry const* pStoneclawTotem;
			SpellEntry const* pTotemOfWrath;

			// Shaman Enhancement Spells
			SpellEntry const* pAstralRecall;
			SpellEntry const* pBloodlust;
			SpellEntry const* pEarthElementalTotem;
			SpellEntry const* pFarSight;
			SpellEntry const* pFireResistanceTotem;
			SpellEntry const* pFlametongueTotem;
			SpellEntry const* pFlametongueWeapon;
			SpellEntry const* pFrostResistanceTotem;
			SpellEntry const* pFrostbrandWeapon;
			SpellEntry const* pGhostWolf;
			SpellEntry const* pGraceofAirTotem;
			SpellEntry const* pHeroism;
			SpellEntry const* pLightningShield;
			SpellEntry const* pNatureResistanceTotem;
			SpellEntry const* pRockbiterWeapon;
			SpellEntry const* pSentryTotem;
			SpellEntry const* pStoneskinTotem;
			SpellEntry const* pStrenghtOfEarthTotem;
			SpellEntry const* pWaterBreathing;
			SpellEntry const* pWindFuryTotem;
			SpellEntry const* pWindFuryWeapon;
			SpellEntry const* pWindwallTotem;
			SpellEntry const* pWrathofAirTotem;
			SpellEntry const* pWaterWalking;
			// Totems
			SpellEntry const* pAirTotem;
			SpellEntry const* pEarthTotem;
			SpellEntry const* pFireTotem;
			SpellEntry const* pWaterTotem;

			// Weapon buff
			SpellEntry const* pWeaponBuff;

			// Shaman Restoration Spells
			SpellEntry const* pAncestralSpirit;
			SpellEntry const* pChainHeal;
			SpellEntry const* pCureDisease;
			SpellEntry const* pCurePoison;
			SpellEntry const* pDiseaseCleansingTotem;
			SpellEntry const* pHealingStreamTotem;
			SpellEntry const* pHealingWave;
			SpellEntry const* pLesserHealingWave;
			SpellEntry const* pManaSpringTotem;
			SpellEntry const* pPoisonCleansingTotem;
			SpellEntry const* pReincarnation;
			SpellEntry const* pTotemicCall;
			SpellEntry const* pTranquilAirTotem;
			SpellEntry const* pTremorTotem;
			SpellEntry const* pWaterShield;

			// Shaman Elemental Talents
			SpellEntry const* pCallOfFlame;
			SpellEntry const* pCallOfThunder;
			SpellEntry const* pConcussion;
			SpellEntry const* pConvection;
			SpellEntry const* pEarthsGrasp;
			SpellEntry const* pElementalDevastation;
			SpellEntry const* pElementalFocus;
			SpellEntry const* pElementalFury;
			SpellEntry const* pElementalMastery;
			SpellEntry const* pElementalPrecision;
			SpellEntry const* pElementalWarding;
			SpellEntry const* pEyeOfTheStorm;
			SpellEntry const* pImprovedFireTotems;
			SpellEntry const* pLightningMastery;
			SpellEntry const* pLightningOverload;
			SpellEntry const* pReverberation;

			// Shaman Enhancement Talents
			SpellEntry const* pAncestralKnowlege;
			SpellEntry const* pAnticipation;
			SpellEntry const* pDualWield;
			SpellEntry const* pDualWieldSpecialization;
			SpellEntry const* pElementalWeapons;
			SpellEntry const* pEnhancingTotems;
			SpellEntry const* pFlurry;
			SpellEntry const* pGuardianTotems;
			SpellEntry const* pImprovedLightningShield;
			SpellEntry const* pImprovedWeaponTotems;
			SpellEntry const* pMentalQuickness;
			SpellEntry const* pShamanisticFocus;
			SpellEntry const* pShamanisticRage;
			SpellEntry const* pShieldSpecialization;
			SpellEntry const* pSpiritWeapons;
			SpellEntry const* pStormstrike;
			SpellEntry const* pThunderingStrikes;
			SpellEntry const* pToughness;

			// Shaman Restoration Talents
			SpellEntry const* pAncestralHealing;
			SpellEntry const* pEarthShield;
			SpellEntry const* pFocusedMind;
			SpellEntry const* pHealingFocus;
			SpellEntry const* pHealingGrace;
			SpellEntry const* pHealingWay;
			SpellEntry const* pImprovedChainHeal;
			SpellEntry const* pImprovedHealingWave;
			SpellEntry const* pImprovedReincarnation;
			SpellEntry const* pManaTideTotem;
			SpellEntry const* pNaturesBlessing;
			SpellEntry const* pNatureGuardian;
			SpellEntry const* pNatureGuidance;
			SpellEntry const* pNaturesSwiftness;
			SpellEntry const* pPurification;
			SpellEntry const* pRestorativeTotems;
		} shaman;
		struct
		{
			// Mage Arcane Spells
			SpellEntry const* pConjureFood;
			SpellEntry const* pConjureWater;
			SpellEntry const* pConjureManaAgate;
			SpellEntry const* pConjureManaCitrine;
			SpellEntry const* pConjureManaEmerald;
			SpellEntry const* pConjureManaJade;
			SpellEntry const* pConjureManaRuby;
			SpellEntry const* pAmplityMagic;
			SpellEntry const* pArcaneBlast;
			SpellEntry const* pArcaneBrilliance;
			SpellEntry const* pArcaneExplosion;
			SpellEntry const* pArcaneIntellect;
			SpellEntry const* pArcaneMissiles;
			SpellEntry const* pBlink;
			SpellEntry const* pCounterspell;
			SpellEntry const* pDampenMagic;
			SpellEntry const* pEvocation;
			SpellEntry const* pInvisibility;
			SpellEntry const* pMageArmor;
			SpellEntry const* pManaShield;
			SpellEntry const* pPolymorph;
			SpellEntry const* pRemoveLesserCurse;
			SpellEntry const* pRitualOfRefreshment;
			SpellEntry const* pSlowFall;
			SpellEntry const* pSpellsteal;
			SpellEntry const* pPortalToDarnassus;
			SpellEntry const* pPortalToExodar;
			SpellEntry const* pPortalToIronForge;
			SpellEntry const* pPortalToOrgrimmar;
			SpellEntry const* pPortalToShattrath;
			SpellEntry const* pPortalToSilvermoon;
			SpellEntry const* pPortalToStonard;
			SpellEntry const* pPortalToStormwind;
			SpellEntry const* pPortalToTeramore;
			SpellEntry const* pPortalToThunderBluff;
			SpellEntry const* pPortalToUndercity;
			SpellEntry const* pTeleportToDarnassus;
			SpellEntry const* pTeleportToExodar;
			SpellEntry const* pTeleportToIronForge;
			SpellEntry const* pTeleportToOrgrimmar;
			SpellEntry const* pTeleportToShattrath;
			SpellEntry const* pTeleportToSilvermoon;
			SpellEntry const* pTeleportToStonard;
			SpellEntry const* pTeleportToStormwind;
			SpellEntry const* pTeleportToTeramore;
			SpellEntry const* pTeleportToThunderBluff;
			SpellEntry const* pTeleportToUndercity;
			SpellEntry const* pPresenceOfMind;
			SpellEntry const* pSlow;

			// Mage Fire Spells
			SpellEntry const* pMoltenArmor;
			SpellEntry const* pFlamestrike;
			SpellEntry const* pScorch;
			SpellEntry const* pFireball;
			SpellEntry const* pFireBlast;
			SpellEntry const* pFireWard;
			SpellEntry const* pBlastWave;

			// Mage Frost Spells
			SpellEntry const* pIceBlock;
			SpellEntry const* pIceLance;
			SpellEntry const* pBlizzard;
			SpellEntry const* pFrostbolt;
			SpellEntry const* pConeOfCold;
			SpellEntry const* pFrostNova;
			SpellEntry const* pFrostArmor;
			SpellEntry const* pFrostWard;
			SpellEntry const* pIceArmor;

			// Mage Arcane Talents
			SpellEntry const* pArcaneConcentration;
			SpellEntry const* pArcaneSubtlety;
			SpellEntry const* pArcaneMeditation;
			SpellEntry const* pWandSpecialization;
			SpellEntry const* pArcaneImpact;
			SpellEntry const* pMagicAttunement;
			SpellEntry const* pImprovedManaShield;
			SpellEntry const* pArcaneFocus;
			SpellEntry const* pArcaneMind;
			SpellEntry const* pArcaneInstability;
			SpellEntry const* pImprovedArcaneMissiles;
			SpellEntry const* pImprovedCounterspell;
			SpellEntry const* pEmpoweredArcaneMissiles;
			SpellEntry const* pArcanePotency;
			SpellEntry const* pMagicAbsorption;
			SpellEntry const* pMindMastery;
			SpellEntry const* pImprovedBlink;
			SpellEntry const* pPrismaticCloak;
			SpellEntry const* pSpellPower;
			SpellEntry const* pArcaneFortitude;
			SpellEntry const* pArcanePower;

			// Mage Fire Talents
			SpellEntry const* pPyroblast;
			SpellEntry const* pDragonsBreath;
			SpellEntry const* pMoltenShields;
			SpellEntry const* pIgnite;
			SpellEntry const* pImprovedFireBlast;
			SpellEntry const* pImprovedScorch;
			SpellEntry const* pImprovedFireball;
			SpellEntry const* pFlameThrowing;
			SpellEntry const* pImprovedFlamestrike;
			SpellEntry const* pCriticalMass;
			SpellEntry const* pIncineration;
			SpellEntry const* pPyromaniac;
			SpellEntry const* pImpact;
			SpellEntry const* pFirePower;
			SpellEntry const* pMasterOfElements;
			SpellEntry const* pBurningSoul;
			SpellEntry const* pPlayingWithFire;
			SpellEntry const* pBlazingSpeed;
			SpellEntry const* pEmpoweredFireball;
			SpellEntry const* pMoltenFury;
			SpellEntry const* pCombustion;

			// Mage Frost Talents
			SpellEntry const* pIceBarier;
			SpellEntry const* pFrostbite;
			SpellEntry const* pWintersChill;
			SpellEntry const* pImprovedBlizzard;
			SpellEntry const* pImprovedConeOfCold;
			SpellEntry const* pImprovedFrostNova;
			SpellEntry const* pShatter;
			SpellEntry const* pPermafrost;
			SpellEntry const* pFrostWarding;
			SpellEntry const* pArcticReach;
			SpellEntry const* pPiercingIce;
			SpellEntry const* pIceShards;
			SpellEntry const* pImprovedFrostbolt;
			SpellEntry const* pFrostChanneling;
			SpellEntry const* pElementalPrecision;
			SpellEntry const* pIceFloes;
			SpellEntry const* pArcticWinds;
			SpellEntry const* pFrozenCore;
			SpellEntry const* pEnpoweredFrostbolt;
			SpellEntry const* pIcyVeins;
			SpellEntry const* pSummonWaterElemental;
			SpellEntry const* pColdSnap;
		} mage;
		struct
		{
			// Warlock Affliction Spells
			SpellEntry const* pCorruption;
			SpellEntry const* pCurseOfAgony;
			SpellEntry const* pCurseOfDoom;
			SpellEntry const* pCurseOfRecklessness;
			SpellEntry const* pCurseOfTheElements;
			SpellEntry const* pCurseOfTongues;
			SpellEntry const* pCurseOfWeakness;
			SpellEntry const* pDeathCoil;
			SpellEntry const* pDrainLife;
			SpellEntry const* pDrainMana;
			SpellEntry const* pDrainSoul;
			SpellEntry const* pFear;
			SpellEntry const* pHowlofTerror;
			SpellEntry const* pLifeTap;
			SpellEntry const* pSeedOfCorruption;
			SpellEntry const* pCurseOfIdiocy;
			SpellEntry const* pUnstableAffliction;

			// Warlock Demonology Spells
			SpellEntry const* pBanish;
			SpellEntry const* pCreateFirestone;
			SpellEntry const* pCreateHealthstone;
			SpellEntry const* pCreateSoulstone;
			SpellEntry const* pCreateSpellstone;
			SpellEntry const* pDemonArmor;
			SpellEntry const* pDetectInvisibility;
			SpellEntry const* pEnslaveDemon;
			SpellEntry const* pEyeOfKilrogg;
			SpellEntry const* pFelArmor;
			SpellEntry const* pHealthFunnel;
			SpellEntry const* pRitualOfDoom;
			SpellEntry const* pRitualOfSouls;
			SpellEntry const* pRitualOfSummoning;
			SpellEntry const* pSenseDemons;
			SpellEntry const* pShadowWard;
			SpellEntry const* pSoulShatter;
			SpellEntry const* pSummonDreadsteed;
			SpellEntry const* pSummonFelhunter;
			SpellEntry const* pSummonImp;
			SpellEntry const* pSummonIncubus;
			SpellEntry const* pSummonSuccubus;
			SpellEntry const* pSummonVoidwalker;
			SpellEntry const* pUnedingBreath;
			SpellEntry const* pDemonicSacrifice;
			SpellEntry const* pSummonFelguard;
			SpellEntry const* pSummonFelsteed;

			// Warlock Destruction Spells
			SpellEntry const* pHellfire;
			SpellEntry const* pImmolate;
			SpellEntry const* pIncinerate;
			SpellEntry const* pRainOfFire;
			SpellEntry const* pSearingPain;
			SpellEntry const* pShadowBolt;
			SpellEntry const* pSoulFire;
			SpellEntry const* pShadowburn;
			SpellEntry const* pShadowfury;

			// Warlock Affliction Talents
			SpellEntry const* pAmplifyCurse;
			SpellEntry const* pContagion;
			SpellEntry const* pCurseOfExhaustion;
			SpellEntry const* pDarkPact;
			SpellEntry const* pEnpoweredCorruption;
			SpellEntry const* pFelConcentration;
			SpellEntry const* pGrimReach;
			SpellEntry const* pImprovedCorruption;
			SpellEntry const* pImprovedCurseOfAgony;
			SpellEntry const* pImprovedCurseOfWeakness;
			SpellEntry const* pImprovedDrainSoul;
			SpellEntry const* pImprovedHowlofTerror;
			SpellEntry const* pImprovedLifeTap;
			SpellEntry const* pMalediction;
			SpellEntry const* pNightfall;
			SpellEntry const* pShadowEmbrace;
			SpellEntry const* pShadowMastery;
			SpellEntry const* pSiphonLife;
			SpellEntry const* pSoulSiphon;

			// Warlock Demonology Talents
			SpellEntry const* pDemonicAegis;
			SpellEntry const* pDemonicEmbrace;
			SpellEntry const* pDemonicKnowledge;
			SpellEntry const* pDemonicResilience;
			SpellEntry const* pFelDomination;
			SpellEntry const* pFelIntellect;
			SpellEntry const* pImprovedEnslaveDemon;
			SpellEntry const* pImprovedHealthFunnel;
			SpellEntry const* pImprovedHealthstone;
			SpellEntry const* pImprovedImp;
			SpellEntry const* pImprovedSayaad;
			SpellEntry const* pImprovedVoidwalker;
			SpellEntry const* pManaFeed;
			SpellEntry const* pManaConjuror;
			SpellEntry const* pMasterDemonologist;
			SpellEntry const* pMasterSummoner;

			// Warlock Destruction Talents
			SpellEntry const* pAfterMath;
			SpellEntry const* pBackLash;
			SpellEntry const* pBane;
			SpellEntry const* pCataclysm;
			SpellEntry const* pConflagrate;
			SpellEntry const* pDestructiveReach;
			SpellEntry const* pDevastation;
			SpellEntry const* pEmberstorm;
			SpellEntry const* pImprovedFirebolt;
			SpellEntry const* pImprovedImmolate;
			SpellEntry const* pEmprovedSearingPain;
			SpellEntry const* pImprovedShadowBolt;
			SpellEntry const* pIntensity;
			SpellEntry const* pNetherProtection;
		} warlock;
		struct
		{
			// Druid Balance Spells
			SpellEntry const* pBarkskin;
			SpellEntry const* pCyclone;
			SpellEntry const* pEntanglingRoots;
			SpellEntry const* pFaerieFire;
			SpellEntry const* pFaerieFireFeral;
			SpellEntry const* pHibernate;
			SpellEntry const* pHurricane;
			SpellEntry const* pInnervate;
			SpellEntry const* pMoonfire;
			SpellEntry const* pSootheAnimal;
			SpellEntry const* pStarfire;
			SpellEntry const* pTeleportMoonglade;
			SpellEntry const* pThorns;
			SpellEntry const* pWrath;
			SpellEntry const* pNaturesGrasp;

			// Druid Feral Combat Spells
			SpellEntry const* pAquaticForm;
			SpellEntry const* pBash;
			SpellEntry const* pBearForm;
			SpellEntry const* pCatForm;
			SpellEntry const* pChallengingRoar;
			SpellEntry const* pClaw;
			SpellEntry const* pCower;
			SpellEntry const* pDash;
			SpellEntry const* pDemoralisationRoar;
			SpellEntry const* pDireBearForm;
			SpellEntry const* pEnrage;
			SpellEntry const* pFelineGrace;
			SpellEntry const* pFerociousBite;
			SpellEntry const* pFlightForm;
			SpellEntry const* pFrenziedRegeneration;
			SpellEntry const* pGrowl;
			SpellEntry const* pLacerate;
			SpellEntry const* pMaim;
			SpellEntry const* pMangleBear;
			SpellEntry const* pMangleCat;
			SpellEntry const* pMaul;
			SpellEntry const* pPounce;
			SpellEntry const* pPrimalFury;
			SpellEntry const* pProwl;
			SpellEntry const* pRake;
			SpellEntry const* pRavage;
			SpellEntry const* pRip;
			SpellEntry const* pShred;
			SpellEntry const* pSwiftFlightForm;
			SpellEntry const* pSwipe;
			SpellEntry const* pTigersFury;
			SpellEntry const* pTrackHumanoids;
			SpellEntry const* pTravelForm;

			// Druid Restoration Spells
			SpellEntry const* pAbolishPoison;
			SpellEntry const* pCurePoison;
			SpellEntry const* pGiftOfTheWild;
			SpellEntry const* pHealingTouch;
			SpellEntry const* pLifebloom;
			SpellEntry const* pMarkOfTheWild;
			SpellEntry const* pRebirth;
			SpellEntry const* pRegrowth;
			SpellEntry const* pRejuvenation;
			SpellEntry const* pRemoveCurse;
			SpellEntry const* pTranquility;

			// Druid Balance Talents
			SpellEntry const* pBalanceOfPower;
			SpellEntry const* pBrambles;
			SpellEntry const* pCelestialFocus;
			SpellEntry const* pControlOfNature;
			SpellEntry const* pDreamState;
			SpellEntry const* pFocusedStarlight;
			SpellEntry const* pForceOfNature;
			SpellEntry const* pImprofedFaerieFire;
			SpellEntry const* pImprovedMoonfire;
			SpellEntry const* pImproveNaturesGrasp;
			SpellEntry const* pInsectSwarm;
			SpellEntry const* pLunarGuidance;
			SpellEntry const* pMoonfury;
			SpellEntry const* pMoonglow;
			SpellEntry const* pMoonkinForm;
			SpellEntry const* pNaturesGrace;
			SpellEntry const* pNaturesReach;
			SpellEntry const* pStarlightWrath;
			SpellEntry const* pVengence;

			// Druid Feral Talents
			SpellEntry const* pBrutalImpact;
			SpellEntry const* pFeralAggression;
			SpellEntry const* pFeralCharge;
			SpellEntry const* pFeralInstinct;
			SpellEntry const* pFeralSwiftness;
			SpellEntry const* pFerocity;
			SpellEntry const* pHeartOfTheWild;
			SpellEntry const* pImprovedLeaderOfThePack;
			SpellEntry const* pLeaderOfThePack;
			SpellEntry const* pNurturingInstinct;
			SpellEntry const* pPredatoryInstincts;
			SpellEntry const* pPredatoryStrikes;
			SpellEntry const* pPrimalTenacity;
			SpellEntry const* pSavageFury;
			SpellEntry const* pSharpenedClaws;
			SpellEntry const* pShreddingAttacks;

			// Druid Restoration Talents
			SpellEntry const* pEmpoweredRejuvenation;
			SpellEntry const* pEmpoweredTouch;
			SpellEntry const* pFuror;
			SpellEntry const* pGiftOfNature;
			SpellEntry const* pImprovedMarkOfTheWild;
			SpellEntry const* pImprovedRegrowth;
			SpellEntry const* pImprovedRejuvenation;
			SpellEntry const* pImprovedTranquility;
			SpellEntry const* pIntensity;
			SpellEntry const* pLivingSpirit;
			SpellEntry const* pNaturalPerfection;
			SpellEntry const* pNaturalShapeshifter;
			SpellEntry const* pNaturalist;
			SpellEntry const* pNaturesFocus;
		} druid;
		struct
		{
			SpellEntry const* pPerception;
			SpellEntry const* pStoneform;
			SpellEntry const* pShadowmeld;
			SpellEntry const* pEscapeArtist;
			SpellEntry const* pGiftOfTheNaaru;
			SpellEntry const* pBloodFury;
			SpellEntry const* pWillOfTheForsaken;
			SpellEntry const* pWarStomp;
			SpellEntry const* pBerserking;
			SpellEntry const* pArcaneTorrent;
		} racial;
	} m_spells{};	
	// ~Constructor Variables

	// Mount Variables
	uint32 mount_spell_id_60 = 30174;
	uint32 mount_spell_id_100 = 30174;
	uint32 mount_flying_spell_id_60 = 30174;
	uint32 mount_flying_spell_id_280 = 30174;
	// ~Mount Variables

	void OnPacketReceived(WorldPacket const& packet) override;
	void ProcessPacket(WorldPacket const& packet) override;
	
	CombatBotRoles m_role = ROLE_INVALID;
	
	static bool IsPhysicalDamageClass(const uint8 playerClass)
	{
		switch (playerClass)
		{
			case CLASS_WARRIOR:
			case CLASS_PALADIN:
			case CLASS_ROGUE:
			case CLASS_HUNTER:
			case CLASS_SHAMAN:
			case CLASS_DRUID:
				return true;
		}
		return false;
	}
	static bool IsRangedDamageClass(const uint8 playerClass)
	{
		switch (playerClass)
		{
			case CLASS_HUNTER:
			case CLASS_PRIEST:
			case CLASS_SHAMAN:
			case CLASS_MAGE:
			case CLASS_WARLOCK:
			case CLASS_DRUID:
				return true;
		}
		return false;
	}
	static bool IsMeleeDamageClass(const uint8 playerClass)
	{
		switch (playerClass)
		{
			case CLASS_WARRIOR:
			case CLASS_PALADIN:
			case CLASS_ROGUE:
			case CLASS_SHAMAN:
			case CLASS_DRUID:
				return true;
		}
		return false;
	}
	static bool IsMeleeWeaponClass(const uint8 playerClass)
	{
		switch (playerClass)
		{
			case CLASS_WARRIOR:
			case CLASS_PALADIN:
			case CLASS_ROGUE:
			case CLASS_SHAMAN:
				return true;
		}
		return false;
	}
	static bool IsTankClass(const uint8 playerClass)
	{
		switch (playerClass)
		{
			case CLASS_WARRIOR:
			case CLASS_PALADIN:
			case CLASS_DRUID:
				return true;
		}
		return false;
	}
	static bool IsHealerClass(const uint8 playerClass)
	{
		switch (playerClass)
		{
			case CLASS_PALADIN:
			case CLASS_PRIEST:
			case CLASS_SHAMAN:
			case CLASS_DRUID:
				return true;
		}
		return false;
	}
	static bool IsStealthClass(const uint8 playerClass)
	{
		switch (playerClass)
		{
			case CLASS_ROGUE:
			case CLASS_DRUID:
				return true;
		}
		return false;
	}

};
