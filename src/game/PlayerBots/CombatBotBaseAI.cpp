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
				if (const Player* pLeader = ObjectAccessor::FindPlayerNotInWorld(pAI->m_leaderGUID))
				{
					if (me->GetGroup()->IsMember(pLeader->GetObjectGuid()))
					{
						p << pLeader->GetObjectGuid();
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
