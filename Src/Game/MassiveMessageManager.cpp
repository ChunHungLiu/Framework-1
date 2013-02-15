
#include "MassiveMessageManager.hpp"
#include <Game/Player.hpp>
#include <System/Tools.h>
#include <System/Log.h>
#include <random>

Game::MassiveMessageManager* TheMassiveMessageMgr = ::new Game::MassiveMessageManager;

namespace Game
{
	MassiveMessageManager::MassiveMessageManager() :
		host(false), 
		ioServicePool(1),
		gameServer(nullptr),
		connectionPending(false),
		connectionFailed(false)
	{
		ioServicePool.Run();
	}

	MassiveMessageManager::~MassiveMessageManager()
	{
		localPlayer.reset(nullptr);
	}

	void MassiveMessageManager::SetPort(uint16_t pPort)
	{
		if(!connectionPending)
		{
			port = pPort;
		}
	}

	void MassiveMessageManager::SetVersion(uint16_t version)
	{
		this->version = version;
	}

	uint16_t MassiveMessageManager::GetVersion()
	{
		return version;
	}

	void MassiveMessageManager::SetAddress(const std::string& pAddress)
	{
		if(!connectionPending)
		{
			address = pAddress;
		}
	}

	void MassiveMessageManager::SetPlayerConstructor(GameServer::PlayerConstructor& ctor)
	{
		playerConstructor = ctor;
	}

	void MassiveMessageManager::SetGOMServerConstructor(GameServer::GOMServerConstructor& ctor)
	{
		gomConstructor = ctor;
	}

	void MassiveMessageManager::BeginMultiplayer(bool pHost)
	{
		if(!gomConstructor)
		{
			throw std::runtime_error("No GOM Server constructor set.");
		}

		host = pHost;
		gomDatabase.reset(new GOMDatabase(gomConstructor(nullptr)));	

#ifndef _SERVER_MODE
		std::random_device rd;
		Player::KeyType key = rd()%std::numeric_limits<int32_t>::max() + 1;
		Player* player = playerConstructor ? playerConstructor(key, nullptr) : new Player(key);
		localPlayer.reset(player);
#endif

		if(host)
		{
			gameServer.reset(nullptr);
			gameServer.reset(new GameServer(port, playerConstructor, gomConstructor));

#ifndef _SERVER_MODE
			player->OnSynchronize();
#endif
		}
		else
		{
			connection.reset(::new Framework::Network::TcpConnection(ioServicePool.GetIoService()));
			Connect(address, std::to_string((long long)port));
		}
	}

	void MassiveMessageManager::Connect(const std::string& pAddress, const std::string& pPort)
	{
		connectionPending = true;
		Framework::System::Log::Debug(std::string("MassiveMessageManager : Connect to ") + pAddress + std::string(" Port ") + pPort);
		connection->OnConnect.connect(boost::bind(&MassiveMessageManager::OnConnect, this, _1));
		connection->Connect(pAddress,pPort);
	}

	void MassiveMessageManager::Update()
	{
		if(gameServer)
			gameServer->Update();

		if(localPlayer)
			localPlayer->Update();

		Framework::System::Log::Flush();
	}

	Player* MassiveMessageManager::GetLocalPlayer()
	{
		return localPlayer.get();
	}

	Player* MassiveMessageManager::GetPlayer(int pKey)
	{
		if(localPlayer && localPlayer->GetKey() == pKey)
			return localPlayer.get();
		if(gameServer)
			return gameServer->GetPlayer(pKey);
		return nullptr;
	}

	void MassiveMessageManager::OnConnect(bool pConnected)
	{
		if(connectionPending)
		{
			connectionPending = false;

			if(pConnected)
			{
				connectionFailed = false;
				localPlayer->SetConnection(connection);

				std::string decKey = Framework::System::RandomData(32);
				std::string encKey = Framework::System::RandomData(32);
				std::string decIV = Framework::System::RandomData(8);
				std::string encIV = Framework::System::RandomData(8);

				Framework::Network::Packet packet(GetVersion(), Framework::Network::Packet::kHandshake);
				packet << decKey << encKey << decIV << encIV;

				localPlayer->SetCipher(new Framework::Crypt::Cipher(encKey, decKey, encIV, decIV));
				localPlayer->Write(packet);
			
				Framework::System::Log::Debug(std::string("MassiveMessageManager : Successfully connected to ") + address + std::string(":") + to_string((_Longlong)port));
				connection->Start();
			}
			else
			{
				connectionFailed = true;
				Framework::System::Log::Debug(std::string("MassiveMessageManager : Connection to ") + address + std::string(":") + to_string((_Longlong)port) + std::string(" failed!"));
			}

			OnConnection(!connectionFailed);
		}
	}

	void MassiveMessageManager::SendMessageTo(int pKey, Framework::Network::Packet& pPacket)
	{
		Player* player = GetPlayer(pKey);

		if(player)
		{
#ifndef _SERVER_MODE
			if(player == localPlayer.get())
			{
				player->ReceivePacket(pPacket);
			}
			else
#endif
			{
				player->Write(pPacket);
			}
		}
		else if(localPlayer)
		{
			if(pKey == kPlayerServer)
			{
				if(Server())
					localPlayer->ReceivePacket(pPacket);
				else
					localPlayer->Write(pPacket);
			}
			else if(pKey == kPlayerSelf)
			{
				localPlayer->ReceivePacket(pPacket);
			}
		}
		if(pKey == kPlayerAll)
		{
			if(gameServer)
				gameServer->SendMessageAll(pPacket);
			else if(localPlayer)
				localPlayer->ReceivePacket(pPacket);
		}
		else if(pKey == kPlayerSynchronized)
		{
			if(gameServer)
				gameServer->SendMessageAllSynchronized(pPacket);
		}
	}

	void MassiveMessageManager::SendMessageAll(Framework::Network::Packet& pPacket)
	{
		SendMessageTo(kPlayerAll, pPacket);
	}

	bool MassiveMessageManager::Server()
	{
		return host;
	}

	GOMDatabase* MassiveMessageManager::GetGOMDatabase() const
	{
		if(gomDatabase)
			return gomDatabase.get();
		return nullptr;
	}

	Framework::Network::IoServicePool& MassiveMessageManager::GetIoServicePool()
	{
		return ioServicePool;
	}
	
	bool MassiveMessageManager::IsConnectionPending()
	{
		return connectionPending;
	}

	bool MassiveMessageManager::ConnectionFailed()
	{
		if(connectionFailed)
		{
			connectionFailed = false;
			return true;
		}

		return false;
	}
	
	void MassiveMessageManager::CancelPendingConnection()
	{
		if(connectionPending)
		{
			connection->Close();
			connectionFailed = false;
			connectionPending = false;
		}
	}
	
}