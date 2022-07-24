#pragma once
#include "Entities/Player.h"

class PlayerAI
{
public:
	explicit PlayerAI(Player* pPlayer) : me(pPlayer) {}
	virtual ~PlayerAI() = default;

	void SetPlayer(Player * player) { me = player; }
	virtual void UpdateAI(uint32 /*diff*/);

	Player* me;
};
