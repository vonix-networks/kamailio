#ifndef __KZ__H
#define __KZ__H

#include "amqp/include/amqpcpp.h"

#include "json.hpp"

using json = nlohmann::json;


namespace kz 
{

std::string rand_string(int max_length);

/*
const int durable      = 0x1;
const int autodelete   = 0x2;
const int active       = 0x4;
const int passive      = 0x8;
const int ifunused     = 0x10;
const int ifempty      = 0x20;
const int global       = 0x40;
const int nolocal      = 0x80;
const int noack        = 0x100;
const int exclusive    = 0x200;
const int nowait       = 0x400;
const int mandatory    = 0x800;
const int immediate    = 0x1000;
const int redelivered  = 0x2000;
const int multiple     = 0x4000;
const int requeue      = 0x8000;
const int internal     = 0x10000;
*/

#define FLAGS_TABLE \
X(none, = 0x0, "none") \
X(durable, = 0x1, "durable") \
X(autodelete, = 0x2, "auto-delete") \
X(active, = 0x4, "active") \
X(passive, = 0x8, "passive") \
X(ifunused, = 0x10, "if-unused") \
X(ifempty, = 0x20, "if-empty") \
X(global, = 0x40, "global") \
X(nolocal, = 0x80, "no-local") \
X(noack, = 0x100, "no-ack") \
X(exclusive, = 0x200, "exclusive") \
X(nowait, = 0x400, "no-wait") \
X(mandatory, = 0x800, "mandatory") \
X(immediate, = 0x1000, "immediate") \
X(redelivered, = 0x2000, "redelivered") \
X(multiple, = 0x4000, "multiple") \
X(requeue, = 0x8000, "requeue") \
X(internal, = 0x10000, "internal")

#define X(a, b, c) a b,
enum AMQPFlag : int {
  FLAGS_TABLE
};
#undef X


template<class ENUM, class UNDERLYING=typename std::underlying_type<ENUM>::type>
class SafeEnum
{
public:
    SafeEnum() : mFlags(0) {}
    SafeEnum( UNDERLYING singleFlag ) : mFlags(singleFlag) {}
    SafeEnum( ENUM singleFlag ) : mFlags(singleFlag) {}
    SafeEnum( const SafeEnum& original ) : mFlags(original.mFlags) {}

    SafeEnum&   operator ^=( UNDERLYING maskValue )    { mFlags ^= maskValue; return *this;  }
    SafeEnum&   operator ^=( ENUM maskValue )    { mFlags ^= maskValue; return *this;  }
    SafeEnum&   operator |=( ENUM addValue )    { mFlags |= addValue; return *this; }
    SafeEnum    operator |( ENUM addValue )     { SafeEnum  result(*this); result |= addValue; return result; }
    SafeEnum&   operator &=( ENUM maskValue )   { mFlags &= maskValue; return *this; }
    SafeEnum&   operator &=( UNDERLYING maskValue )   { mFlags &= maskValue; return *this; }
    SafeEnum    operator &( ENUM maskValue )    { SafeEnum  result(*this); result &= maskValue; return result; }
    SafeEnum    operator &( ENUM maskValue ) const   { SafeEnum  result(*this); result &= maskValue; return result; }
    SafeEnum    operator ~()    { SafeEnum  result(*this); result.mFlags = ~result.mFlags; return result; }
    explicit    operator bool()                    { return mFlags != 0; }
    explicit    operator bool()  const             { return mFlags != 0; }
    operator UNDERLYING()              { return mFlags; }
    operator UNDERLYING()  const       { return mFlags; }

protected:
    UNDERLYING  mFlags;
};


typedef SafeEnum<AMQPFlag> Flag;

template < class T >
std::ostream& operator << (std::ostream& os, const std::vector<T>& v)
{
    os << "[";
    bool first = true;
    for (typename std::vector<T>::const_iterator ii = v.begin(); ii != v.end(); ++ii)
    {
    	if(!first) os << " , ";
        os << " " << *ii;
        first = false;
    }
    os << "]";
    return os;
}

template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

template <class T>
class Singleton {
public:
  static T& getInstance(){
    static T instance;
    // volatile int dummy{};
    return instance;
  }
private:
  Singleton()= default;
  ~Singleton()= default;
  Singleton(const Singleton&)= delete;
  Singleton& operator=(const Singleton&)= delete;
};

class Exchange;
typedef std::vector<std::string> RoutingKeys;
typedef std::map<std::string,Exchange> Exchanges;
typedef Singleton<Exchanges> GlobalExchanges;



class ExchangeBinding
{
public:
	ExchangeBinding() {};

	ExchangeBinding(const Exchange& exchange, std::vector<std::string> keys = {}, const AMQP::Table& arguments = AMQP::Table()) :
	   _exchange(std::make_shared<Exchange>(exchange)),
	   _keys(keys),
	   _arguments(std::move(arguments))
       {};

	ExchangeBinding(const ExchangeBinding& Binding) :
		   _exchange(Binding._exchange),
		   _keys(Binding._keys),
		   _arguments(Binding._arguments)
	       {};



	void addKey(std::string key) { _keys.push_back(key); }

	const Exchange& exchange() const
	{
		return *_exchange;
	};

	const RoutingKeys& routing() const
	{
		return _keys;
	};

	const AMQP::Table& arguments() const
	{
		return _arguments;
	};

private:
   std::shared_ptr<Exchange> _exchange;
   std::vector<std::string> _keys;
   AMQP::Table _arguments;
};



class ExchangeBindings
{
private:
	std::map<std::string, ExchangeBinding> _map;

public:
	ExchangeBindings() {}

	bool add(std::string exchange, std::vector<std::string> keys, const AMQP::Table& arguments = AMQP::Table());
	bool add(const Exchange& exchange, std::vector<std::string> keys, const AMQP::Table& arguments = AMQP::Table());

	const std::map<std::string, ExchangeBinding>& map() const
	{
		return _map;
	}

};

class Exchange
{
private:

	AMQP::ExchangeType _init_amqp_type(std::string _name)
	{
		return type == "topic" ? AMQP::ExchangeType::topic :
		type == "direct" ? AMQP::ExchangeType::direct :
		type == "x-consistent-hash" ? AMQP::ExchangeType::consistent_hash :
		type == "fanout" ? AMQP::ExchangeType::fanout :
		type == "headers" ? AMQP::ExchangeType::headers :
		AMQP::ExchangeType::topic;
	}

public:
	Exchange() :
		amqp_type(AMQP::ExchangeType::topic)
	{};

	Exchange(std::string _name, std::string _type = "topic", const AMQP::Table& _arguments = AMQP::Table()) :
		name(_name),
		type(_type),
		options(autodelete),
		arguments(std::move(_arguments))
		{
		amqp_type = _init_amqp_type(_type);
		};

	Exchange(const Exchange& copy) :
		name(copy.name),
		type(copy.type),
		options(copy.options),
		arguments(copy.arguments),
		bindings(copy.bindings),
		amqp_type(copy.amqp_type),
		delete_on_error(copy.delete_on_error)
		{
		};


    bool addBinding(std::string exchange, std::vector<std::string> keys, const AMQP::Table& arguments = AMQP::Table());
    bool addBinding(const Exchange& exchange, std::vector<std::string> keys, const AMQP::Table& arguments = AMQP::Table());

   std::string name;
   std::string type;
   Flag options;
   AMQP::Table arguments;
   ExchangeBindings bindings;
   AMQP::ExchangeType amqp_type;
   bool delete_on_error = false;
};





class QueueBinding
{
public:
	QueueBinding() {};

	QueueBinding(const Exchange& exchange, std::vector<std::string> keys = {}, bool federated = false, const AMQP::Table& arguments = AMQP::Table()) :
	   _exchange(std::make_shared<Exchange>(exchange)),
	   _keys(keys),
	   _federated(federated),
	   _arguments(std::move(arguments))
       {};

	QueueBinding(const QueueBinding& Binding) :
		   _exchange(Binding._exchange),
		   _keys(Binding._keys),
		   _federated(Binding._federated),
		   _arguments(Binding._arguments)
	       {};



	void addKey(std::string key) { _keys.push_back(key); }


	const Exchange& exchange() const
	{
		return *_exchange;
	};

	const bool IsFederated() const
	{
		return _federated;
	};

	void IsFederated(bool federated)
	{
		_federated = federated;
	};

	const RoutingKeys& routing() const
	{
		return _keys;
	};

	const AMQP::Table& arguments() const
	{
		return _arguments;
	};

private:
   std::shared_ptr<Exchange> _exchange;
   std::vector<std::string> _keys;
   bool _federated;
   AMQP::Table _arguments;
};

class QueueBindings
{
private:
	std::map<std::string, QueueBinding> _map;

public:
	QueueBindings() {}
	QueueBindings(const QueueBindings& bindings) :
		_map(bindings._map)
	{
	}

	bool add(std::string exchange, std::vector<std::string> keys, bool federated = false, const AMQP::Table& arguments = AMQP::Table());
	bool add(const Exchange& exchange, std::vector<std::string> keys, bool federated = false, const AMQP::Table& arguments = AMQP::Table());

	const std::map<std::string, QueueBinding>& map() const
	{
		return _map;
	}

};

class Qos
{
public:
      Qos() : is_shared(false), prefetch_count(0) {};

   bool is_shared;
   int  prefetch_count;
};


class Consume
{
public:
      Consume() : /*options(),*/ qos(), options(exclusive | nolocal | noack | nowait) {};
      Consume(const Consume& consume) :
    	  qos(consume.qos),
    	  options(consume.options),
    	  arguments(consume.arguments)
      {};

//   ConsumeOptions options;
   Qos qos;
   Flag options;
   AMQP::Table arguments;


   // flags + arguments aqui
};


class Queue
{
public:
      Queue() : options(exclusive | autodelete) { name = ""; };
      Queue(const std::string& _name) : options(exclusive | autodelete) { name = _name;};
      Queue(const Queue& queue) :
    	  name(queue.name),
    	  options(queue.options),
    	  arguments(queue.arguments),
    	  bindings(queue.bindings),
    	  consume(queue.consume)
    	  {};

      bool addBinding(std::string exchange, std::vector<std::string> keys, bool federated = false, const AMQP::Table& arguments = AMQP::Table());
      bool addBinding(const Exchange& exchange, std::vector<std::string> keys, bool federated = false, const AMQP::Table& arguments = AMQP::Table());

   std::string name;

   /*
    * QueueOptions options;
    * option goes here as flags + arguments
    * flags[AMQP::exclusive] = true
    */
   Flag options;
   AMQP::Table arguments;

   QueueBindings bindings;
   Consume consume;
};


class Listener
{
public:
	Listener() :
		name(rand_string(8)),
		num_channels(1),
		num_workers(1),
		num_federated_workers(1),
		consistent_worker(false),
		consistent_worker_key(""),
		publisher_confirms(false)
		{};

    std::string name;

    int num_channels;
    int num_workers;
    int num_federated_workers;
    bool  consistent_worker;
    std::string consistent_worker_key;
    bool consumer_spawn = false;
    bool deliver_local = false;
    bool publisher_confirms = false;
    AMQP::Table arguments;
    std::vector<Queue> queues;
    std::vector<Exchange> exchanges;
};


class ConnectionInfo
{
private:
	AMQP::Table _properties;
	std::string _amqp_uri;
	std::shared_ptr<AMQP::Address> _address;

	const static std::string default_uri;


public:
	ConnectionInfo(const std::string& uri = default_uri, AMQP::Table properties = AMQP::Table()) :
		_properties(std::move(properties)),
		_amqp_uri(uri),
		_address(std::make_shared<AMQP::Address>(uri))
	{
	}

	const AMQP::Table& properties() const
	{
		return _properties;
	}

	void set_properties(const AMQP::Table& props)
	{
		_properties = std::move(props);
	}

	const AMQP::Address& uri() const
	{
		return *_address;
	}

	void set_uri(std::string uri)
	{
		_amqp_uri = uri;
		_address = std::make_shared<AMQP::Address>(uri);
	}


    const AMQP::Field &get(const std::string &name) const
    {
    	return _properties.get(name);
    };

    /**
     *  Get a field
     *
     *  @param  name    field name
     */
    AMQP::AssociativeFieldProxy operator[](const std::string& name)
    {
        return AMQP::AssociativeFieldProxy(&_properties, name);
    }

    /**
     *  Get a field
     *
     *  @param  name    field name
     */
    AMQP::AssociativeFieldProxy operator[](const char *name)
    {
        return AMQP::AssociativeFieldProxy(&_properties, name);
    }

    /**
     *  Get a const field
     *
     *  @param  name    field name
     */
    const AMQP::Field &operator[](const std::string& name) const
    {
        return _properties.get(name);
    }

};

typedef std::map<std::string,ConnectionInfo> Connections;
typedef Singleton<Connections> GlobalConnections;


}

#endif
