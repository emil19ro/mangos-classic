#pragma once
#include "Entities/Player.h"

class PlayerAI_NYCTERMOON
{
public:
	explicit PlayerAI_NYCTERMOON(Player* pPlayer) : me(pPlayer) {}
	virtual ~PlayerAI_NYCTERMOON() = default;

	void SetPlayer(Player * player) { me = player; }
	virtual void UpdateAI(uint32 /*diff*/);

	Player* me;
};
