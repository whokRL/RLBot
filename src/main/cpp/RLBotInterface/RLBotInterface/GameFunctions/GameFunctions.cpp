#include <CapnProto\capnproto.hpp>
#include "game_data.pb.h"
#include <DebugHelper.hpp>
#include <boost\interprocess\ipc\message_queue.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost\interprocess\sync\named_sharable_mutex.hpp>
#include <boost\interprocess\sync\sharable_lock.hpp>

#include "GameFunctions.hpp"
#include <BoostConstants\BoostConstants.hpp>

#include "..\CallbackProcessor\CallbackProcessor.hpp"

#include <chrono>
#include <thread>

namespace GameFunctions
{
	static boost::interprocess::shared_memory_object gameTickShm(
		boost::interprocess::open_only, BoostConstants::GameDataSharedMemName, boost::interprocess::read_only);

	static boost::interprocess::named_sharable_mutex gameTickMutex(
		boost::interprocess::open_only, BoostConstants::GameDataMutexName);

	extern "C" ByteBuffer RLBOT_CORE_API UpdateLiveDataPacketCapnp()
	{
		return fetchByteBufferFromSharedMem(&gameTickShm, &gameTickMutex);
	}

	static boost::interprocess::shared_memory_object fieldInfoShm(
		boost::interprocess::open_only, BoostConstants::FieldInfoSharedMemName, boost::interprocess::read_only);

	static boost::interprocess::named_sharable_mutex fieldInfoMutex(
		boost::interprocess::open_only, BoostConstants::FieldInfoMutexName);

	extern "C" ByteBuffer RLBOT_CORE_API UpdateFieldInfoCapnp()
	{
		return fetchByteBufferFromSharedMem(&fieldInfoShm, &fieldInfoMutex);
	}

	static boost::interprocess::shared_memory_object gameTickFlatShm(
		boost::interprocess::open_only, BoostConstants::GameDataFlatSharedMemName, boost::interprocess::read_only);

	static boost::interprocess::named_sharable_mutex gameTickFlatMutex(
		boost::interprocess::open_only, BoostConstants::GameDataFlatMutexName);

	extern "C" ByteBuffer RLBOT_CORE_API UpdateLiveDataPacketFlatbuffer()
	{
		return fetchByteBufferFromSharedMem(&gameTickFlatShm, &gameTickFlatMutex);
	}

	static boost::interprocess::shared_memory_object fieldInfoFlatShm(
		boost::interprocess::open_only, BoostConstants::FieldInfoFlatSharedMemName, boost::interprocess::read_only);

	static boost::interprocess::named_sharable_mutex fieldInfoFlatMutex(
		boost::interprocess::open_only, BoostConstants::FieldInfoFlatMutexName);

	extern "C" ByteBuffer RLBOT_CORE_API UpdateFieldInfoFlatbuffer()
	{
		return fetchByteBufferFromSharedMem(&fieldInfoFlatShm, &fieldInfoFlatMutex);
	}

	extern "C" RLBotCoreStatus RLBOT_CORE_API UpdateLiveDataPacket(LiveDataPacket* pLiveData)
	{
		ByteBuffer capnp = UpdateLiveDataPacketCapnp();
		CapnConversions::capnpGameTickToStruct(capnp, pLiveData);
		delete[] capnp.ptr;

		ByteBuffer fieldInfo = UpdateFieldInfoCapnp();
		CapnConversions::applyFieldInfoToStruct(fieldInfo, pLiveData);
		delete[] fieldInfo.ptr;

		return RLBotCoreStatus::Success;
	}

	extern "C" ByteBuffer RLBOT_CORE_API UpdateLiveDataPacketProto()
	{
		ByteBuffer capnp = UpdateLiveDataPacketCapnp();
		return CapnConversions::capnpGameTickToProtobuf(capnp);
	}

	extern "C" ByteBuffer RLBOT_CORE_API UpdateFieldInfoProto()
	{
		ByteBuffer capnp = UpdateFieldInfoCapnp();
		return CapnConversions::capnpFieldInfoToProtobuf(capnp);
	}

	extern "C" RLBotCoreStatus RLBOT_CORE_API SetGameState(void* gameTickPacket, int protoSize, CallbackFunction callback, unsigned int* pID)
	{
		rlbot::api::GameTickPacket* protoResult = &rlbot::api::GameTickPacket();
		protoResult->ParseFromArray(gameTickPacket, protoSize);

		// TODO: convert to canproto and send to core via a message queue
		return RLBotCoreStatus::Success;
	}

	extern "C" RLBotCoreStatus RLBOT_CORE_API UpdatePlayerInputProto(void* playerInputBinary, int protoSize)
	{
		rlbot::api::PlayerInput protoInput = rlbot::api::PlayerInput();
		protoInput.ParseFromArray(playerInputBinary, protoSize);
		rlbot::api::ControllerState protoState = protoInput.controller_state();

		::capnp::MallocMessageBuilder message;
		rlbot::PlayerInput::Builder capnInput = message.initRoot<rlbot::PlayerInput>();
		capnInput.setPlayerIndex(protoInput.player_index());
		
		rlbot::ControllerState::Builder capnState = capnInput.initControllerState();
		capnState.setBoost(protoState.boost());
		capnState.setHandbrake(protoState.handbrake());
		capnState.setJump(protoState.jump());
		capnState.setPitch(protoState.pitch());
		capnState.setRoll(protoState.roll());
		capnState.setSteer(protoState.steer());
		capnState.setThrottle(protoState.throttle());
		capnState.setYaw(protoState.yaw());

		ByteBuffer buf = CapnConversions::toBuf(&message);

		RLBotCoreStatus status = UpdatePlayerInputCapnp(buf.ptr, buf.size);

		delete[] buf.ptr;

		return status;
	}

	// Currently we are relying on the core dll to create the queue in shared memory before this process starts. TODO: be less fragile
	static interop_message_queue flatPlayerInput(boost::interprocess::open_only, BoostConstants::PlayerInputFlatQueueName);

	extern "C" RLBotCoreStatus RLBOT_CORE_API UpdatePlayerInputFlatbuffer(void* playerInput, int size)
	{
		bool sent = flatPlayerInput.try_send(playerInput, size, 0);
		if (!sent) {
			return RLBotCoreStatus::BufferOverfilled;
		}
		return RLBotCoreStatus::Success;
	}

	static interop_message_queue capnpPlayerInput(boost::interprocess::open_only, BoostConstants::PlayerInputQueueName);

	extern "C" RLBotCoreStatus RLBOT_CORE_API UpdatePlayerInputCapnp(void* controllerState, int protoSize)
	{
		bool sent = capnpPlayerInput.try_send(controllerState, protoSize, 0);
		if (!sent) {
			return RLBotCoreStatus::BufferOverfilled;
		}
		return RLBotCoreStatus::Success;
	}

	extern "C" void RLBOT_CORE_API Free(void* ptr)
	{
		free(ptr);
	}

	extern "C" RLBotCoreStatus RLBOT_CORE_API StartMatch(MatchSettings matchSettings, CallbackFunction callback, unsigned int* pID)
	{
		int numPlayers = matchSettings.NumPlayers;
		//ToDo: Check the other settings
		RLBotCoreStatus status = checkPlayerConfiguration(matchSettings.PlayerConfiguration, numPlayers);

		if (status != RLBotCoreStatus::Success)
			return status;

		BEGIN_GAME_FUNCTION(StartMatchMessage, pStartMatch);
		REGISTER_CALLBACK(pStartMatch, callback, pID);
		pStartMatch->MatchSettings = matchSettings;
		END_GAME_FUNCTION;

		return RLBotCoreStatus::Success;
	}

	extern "C" RLBotCoreStatus RLBOT_CORE_API UpdatePlayerInput(const PlayerInput& playerInput, int playerIndex)
	{
		RLBotCoreStatus status = checkInputConfiguration(playerInput);

		if (status != RLBotCoreStatus::Success)
			return status;

		::capnp::MallocMessageBuilder message;
		rlbot::PlayerInput::Builder capnInput = message.initRoot<rlbot::PlayerInput>();
		capnInput.setPlayerIndex(playerIndex);

		rlbot::ControllerState::Builder capnState = capnInput.initControllerState();
		capnState.setBoost(playerInput.Boost);
		capnState.setHandbrake(playerInput.Handbrake);
		capnState.setJump(playerInput.Jump);
		capnState.setPitch(playerInput.Pitch);
		capnState.setRoll(playerInput.Roll);
		capnState.setSteer(playerInput.Steer);
		capnState.setThrottle(playerInput.Throttle);
		capnState.setYaw(playerInput.Yaw);

		ByteBuffer buf = CapnConversions::toBuf(&message);
		status = UpdatePlayerInputCapnp(buf.ptr, buf.size);

		delete[] buf.ptr;
		return status;
	}

	extern "C" RLBotCoreStatus RLBOT_CORE_API SendChat(QuickChatPreset quickChatPreset, int playerIndex, bool bTeam, CallbackFunction callback, unsigned int* pID)
	{
		RLBotCoreStatus status = checkQuickChatPreset(quickChatPreset);

		if (status != RLBotCoreStatus::Success)
			return status;

		BEGIN_GAME_FUNCTION(SendChatMessage, pSendChat);
		REGISTER_CALLBACK(pSendChat, callback, pID);
		pSendChat->QuickChatPreset = quickChatPreset;
		pSendChat->PlayerIndex = playerIndex;
		pSendChat->bTeam = bTeam;
		END_GAME_FUNCTION;

		return RLBotCoreStatus::Success;
	}
}