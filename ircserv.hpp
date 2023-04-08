#ifndef IRCSERV_HPP
#define IRCSERV_HPP

#include <iostream>
#include <map>
#include <netinet/in.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/_types/_size_t.h>
#include <sys/poll.h>
#include <vector>

#define BlockingError(func) \
	std::cerr << func << ": " << strerror(EWOULDBLOCK) << std::endl
#define Close(sockfd)        \
	if (close(sockfd) == -1) \
	BlockingError("close")
#define ERR_UNKNOWNCOMMAND(cmd) \
	Message(421).addParam(cmd).addParam(":Unknown command")
#define QUIT(usr)                                         \
	Message().setPrefix(usr).setCommand("QUIT").addParam( \
		":User quit unexpectedly")

class User;
struct Message
{
	std::string command;
	std::string prefix;
	std::string totxt() const;
	std::vector<std::string> params;

	friend class Server;
	Message &addParam(const std::string &prm);
	Message &setCommand(const std::string &cmd);
	Message();
	Message(const int ncmd);
	Message(std::string &msg);
	Message &setPrefix(const User &usr);
};

class User
{
	bool hasSecret;
	const int fd;
	std::string buf;
	bool is_oper;
	std::string hostname;
	std::string nickname;
	std::string realname;
	std::string servername;
	std::string username;

	friend class Server;
	friend struct Message;
	friend class Channel;
	User(const int fd);
	bool isRegistered() const;
	static bool validNick(const std::string &nick);
};

class Channel
{
	friend class Server;
	bool hasExternalMessages;
	bool hasProtectedTopic;
	bool is_invite_only;
	bool isInviteOnly;
	bool isModerated;
	bool isPrivate;
	bool isSecret;
	const std::string name;
	size_t limit;
	size_t max_members;
	std::vector<std::string> banMasks;
	std::vector<User *> invited;
	std::set<User *> speakers;
	std::string chTopic;
	std::string key;
	std::string topic;

	struct ChannelMember
	{
		User &usr;
		bool is_oper;
		bool is_banned;
		ChannelMember(User &usr) : usr(usr), is_oper(false)
		{
		}
	};
	std::vector<ChannelMember> members;

	friend bool operator==(const ChannelMember &mem, const User &user)
	{
		return mem.usr.nickname == user.nickname;
	}

	friend bool operator==(const Channel &chn, const std::string &name)
	{
		return name == chn.name;
	}

	friend class Server;

  public:
	Channel(const std::string &name, User &usr, std::string key = std::string())
		: hasExternalMessages(false), hasProtectedTopic(true),
		  isInviteOnly(false), isModerated(false), isPrivate(false),
		  isSecret(false), name(name), limit(100), key(key)
	{
		ChannelMember newMember(usr);
		newMember.is_oper = true;
		members.push_back(newMember);
	}

	int join(User &usr, std::string _key)
	{
		if (this->isMember(usr))
		{
			std::cout << "User is already a member" << std::endl;
			return (4);
		}
		if (is_invite_only &&
			std::find(invited.begin(), invited.end(), &usr) == invited.end())
			return (1);
		if (!key.empty() &&
			key !=
				_key) // still need to no if key is ignored when it is not set in channel
			return (2);
		if (members.size() == limit)
			return (3);
		else
		{
			members.push_back(ChannelMember(usr));
			return (0);
		}
	}

	void broadcast(const std::string &res, const User &skip) const
	{
		std::vector<ChannelMember>::const_iterator it;
		for (it = members.begin(); it != members.end(); it++)
			if (skip.nickname != (*it).usr.nickname)
				if (send(it->usr.fd, res.data(), res.size(), 0) == -1)
					BlockingError("send");
	}

	std::string get_memebers(void)
	{
		std::string membersList;
		std::vector<ChannelMember>::iterator it;
		for (it = members.begin(); it != members.end(); it++)
		{
			if (it->is_oper)
				membersList += '@';
			membersList += it->usr.nickname;
			membersList += ' ';
		}
		return (membersList);
	}

	User *lookUpUser(const std::string &nick)
	{
		for (std::vector<ChannelMember>::iterator it = members.begin();
			 it != members.end();
			 it++)
			if (nick == (*it).usr.nickname)
				return &it->usr;
		return (nullptr);
	}

	bool isMember(User &user) const
	{
		for (std::vector<ChannelMember>::const_iterator it = members.begin();
			 it != members.end();
			 it++)
			if (user.nickname == it->usr.nickname)
				return true;
		return false;
	}
	Channel(const std::string &name, User *usr);
	void kick(User *user)
	{
		members.erase(std::find(members.begin(), members.end(), *user));
	}
	std::string getChannelModes() const
	{
		std::string modes;
		return modes;
	}

	bool isOperator(const User &usr) const
	{
		for (std::vector<ChannelMember>::const_iterator it = members.begin();
			 it != members.end();
			 it++)
			if (usr.nickname == it->usr.nickname)
				return it->is_oper;
		return false;
	}

	const Message addOperator(const std::string &target, const bool add)
	{
		User *mem = lookUpUser(target);
		if (!mem)
			return Message(441).addParam(target).addParam(name).addParam(
				":They aren't on that channel");
		(void)add;
		return Message().setCommand("REPLY").addParam(":Operator has been added");
	}

	const Message setChannelLimit(const std::string &limit)
	{
		ssize_t val = atoi(limit.data());
		if (val >= (ssize_t)members.size())
			this->limit = val;
		else
			return Message().setCommand("REPLY").addParam(
				":Limit is below members count");
		return Message()
			.setCommand("REPLY")
			.addParam(":Limit has been set to")
			.addParam(limit);
	}

	const Message setBanMask(const std::string &mask, const bool add)
	{
		return add ? banMasks.push_back(mask)
				   : (void)banMasks.erase(
						 find(banMasks.begin(), banMasks.end(), mask)),
			   Message().setCommand("REPLY").addParam(":Ban mask has been added");
	}

	const Message setSpeaker(const std::string &user, const bool add)
	{
		if (add)
		{
			if (User *mem = lookUpUser(user))
				speakers.insert(mem);
			return Message(401).addParam(name).addParam(":No such nick/channel");
		}
		else
		{
			if (User *mem = lookUpUser(user))
				speakers.erase(std::find(speakers.begin(), speakers.end(), mem));
			return Message(401).addParam(name).addParam(":No such nick/channel");
		}
		return Message().setCommand("REPLY").addParam(":Speaker has been added");
	}

	const Message setSecret(const std::string &key, const bool add)
	{
		if (add)
			this->key = key;
		else
			this->key.clear();
		return Message().setCommand("REPLY").addParam(":Secret has been set");
	}
};

class Server
{
	const int tcpsock;
	const std::string password;
	sockaddr_in serv;
	std::vector<Channel> channels;
	std::map<const int, User> users;
	std::vector<pollfd> cons;

	~Server();
	bool nickIsUsed(const std::string &nick) const;
	Message pass(User &usr, const Message &req);
	Message user(User &usr, const Message &req);
	Message nick(User &usr, const Message &req);
	Message privmsg(User &usr, const Message &req);
	Message notice(User &usr, const Message &req);
	Message quit(User &usr, const Message &req);

	Message join(User &usr, const Message &req);
	Message list(User &usr, const Message &req);
	Message kick(User &usr, const Message &req);
	Message invite(User &usr, const Message &req);

	Message mode(User &usr, const Message &req);
	Message names(User &usr, const Message &req);
	Message part(User &usr, const Message &req);
	Message topic(User &usr, const Message &req);

	Channel *lookUpChannel(const std::string &chn);
	Server(const int port, const std::string &name);
	User *lookUpUser(const std::string &nick);
	void process(User &usr, const Message &req);
	void receive(std::vector<pollfd>::const_iterator &con);
	void Send(const Message &res, const User &usr)
	{
		if (!res.command.empty())
		{
			const std::string _tmp = res.totxt();
			if (std::cout << "Sending: " << '"' << _tmp << '"' << std::endl,
				send(usr.fd, _tmp.data(), _tmp.size(), 0) == -1)
				BlockingError("send");
		}
	}

  public:
	static Server &getInstance(const int port, const std::string &pass);
	void eventloop();
};

class SystemException : public std::system_error
{
  public:
	SystemException(const char *err)
		: system_error(std::error_code(errno, std::system_category()), err)
	{
	}
};
#endif