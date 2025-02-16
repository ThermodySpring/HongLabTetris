#include "Hub.h"
#include "User/User.h"
#include "User/Session.h"
#include "Command/Commands.h"
#include "Translation.h"

Hub::Hub(uint32_t id, Server& server, boost::asio::io_context& io, std::size_t roomSize)
	: mID(id)
	, mServer(server)
	, mIO(io)
	, mStrand(boost::asio::make_strand(io))
{
	mUsers.reserve(roomSize);
}

void Hub::AddEvent(uint32_t requestUserID, std::shared_ptr<ICommand> command)
{
	if (command->IsFailed())
	{
		return;
	}

	auto self = shared_from_this();

	boost::asio::post
	(mStrand,
	 [this, self = std::move(self), requestUserID, command = std::move(command)]() mutable
	 {
		 if (requestUserID != 0 && this->HasUser(requestUserID))
		 {
			 command->Execute(requestUserID, *this);
		 }
	 });
}

void Hub::AddUser(std::shared_ptr<User> user)
{
	assert(user != nullptr);
	uint32_t id = user->GetID();
	mUsers.emplace(id, std::move(user));
	AddUserEvent(id);
}

void Hub::RemoveUser(uint32_t userID)
{
	RemoveUserEvent(userID);
	mUsers.erase(userID);
}

void Hub::AddUserEvent(uint32_t userID)
{
}

void Hub::RemoveUserEvent(uint32_t userID)
{
}

void Hub::ShutDown()
{
	mUsers.clear();
}

void Hub::BroadcastChatMessage(uint32_t userID, std::string_view message) const
{
	std::string userName = Translation::translate("Anonymous User");

	//userID가 0인 경우 userName 생략
	Users::const_iterator userIt = mUsers.find(userID);
	if (userIt != mUsers.end())
	{
		userName = userIt->second->GetName();
	}

	for (auto user : mUsers)
	{
		// TODO: 조건검사 추가 가능 (도배시 채팅 차단 등)
		user.second->SendCommandToSession(S2C::SendChatMessage(userName, message));
	}
}

uint32_t Hub::GetID() const
{
	return mID;
}

Hub::Strand Hub::GetStrand() const
{
	return mStrand;
}

std::shared_ptr<User> Hub::GetUserOrNull(uint32_t userID)
{
	if (HasUser(userID))
	{
		return mUsers[userID];
	}
	return nullptr;
}

std::shared_ptr<Session> Hub::GetUserSessionOrNull(uint32_t userID)
{
	if (HasUser(userID))
	{
		return mUsers[userID]->GetSession();
	}
	return nullptr;
}

void Hub::SendCommandToUser(uint32_t userID, const ICommand& command) const
{
	Users::const_iterator userIt = mUsers.find(userID);
	if (userIt != mUsers.end())
	{
		userIt->second->SendCommandToSession(command);
	}
}

bool Hub::IsJoinable() const
{
	return mCanEnter;
}

bool Hub::HasUser(uint32_t userID) const
{
	return mUsers.find(userID) != mUsers.end();
}

void Hub::RequestMoveUser(uint32_t userID, std::shared_ptr<Hub> targetHub)
{
	if (targetHub == nullptr || this->HasUser(userID) == false)
	{
		return;
	}

	auto self = shared_from_this();

	Strand targetStrand = targetHub->GetStrand();

	boost::asio::post
	(targetStrand,
	 [this, self = std::move(self), userID, targetHub, targetStrand]() mutable
	 {
		 if (targetHub->IsJoinable() == false)
		 {
			 boost::asio::post
			 (mStrand,
			  [this, self = std::move(self), userID]() mutable
			  {
				  this->RequestCompletionHandler(false, userID);
			  });
			 targetHub->AcceptCompletionHandler(false, userID);
			 return;
		 }

		 boost::asio::post
		 (mStrand,
		  [this, self = std::move(self), userID, targetHub, targetStrand]() mutable
		  {
			  mUsers[userID]->SetHub(nullptr);
			  std::shared_ptr<User> user = std::move(mUsers[userID]);
			  this->RemoveUser(userID);
			  boost::asio::post
			  (targetStrand,
			   [self = std::move(self), userID, targetHub, user = std::move(user)]() mutable
			   {
				   if (user->IsConnectionClosed())
				   {
					   return;
				   }
				   user->SetHub(targetHub);
				   targetHub->AddUser(std::move(user));
				   targetHub->AcceptCompletionHandler(true, userID);
			   }
			  );
			  this->RequestCompletionHandler(true, userID);
		  }
		 );
	 });
}

void Hub::RequestCompletionHandler(bool canMove, uint32_t userID)
{
}

void Hub::AcceptCompletionHandler(bool canAccept, uint32_t userID)
{
}
