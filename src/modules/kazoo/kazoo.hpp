/* Reconstructed (2026-09): umbrella for the C++ AMQP layer (vendored
 * AMQP-CPP under amqp/, libev handler, pipe workers, helpers under include/). */
#ifndef KAZOO_HPP_
#define KAZOO_HPP_

#include "kz.h"
#include "include/kz_log.hpp"
#include "include/kz.hpp"
#include "include/amqp_ows.hpp"
#include "include/amqp_serialize.hpp"
#include "include/kz_ev_handler.hpp"
#include "include/pipe.hpp"
#include "include/pipe_loop.hpp"
#include "include/uuid.hpp"
#include "include/date.hpp"

/* The (de)serializers are defined in kz_amqp_manager.cpp after their first
 * use; nlohmann::json resolves them at the call site, so declare them here. */
namespace kz {
using json = nlohmann::json;
void to_json(json& j, const ConnectionInfo& p);
void from_json(const json& j, ConnectionInfo& p);
void to_json(json& j, const ExchangeBindings& p);
void from_json(const json& j, ExchangeBindings& p);
void to_json(json& j, const Exchange& p);
void from_json(const json& j, Exchange& p);
void to_json(json& j, const QueueBindings& p);
void from_json(const json& j, QueueBindings& p);
void to_json(json& j, const Qos& p);
void from_json(const json& j, Qos& p);
void to_json(json& j, const Flag& p);
void from_json(const json& j, Flag& p);
void to_json(json& j, const Consume& p);
void from_json(const json& j, Consume& p);
void to_json(json& j, const Queue& p);
void from_json(const json& j, Queue& p);
void to_json(json& j, const Listener& p);
void from_json(const json& j, Listener& p);
}

#endif /* KAZOO_HPP_ */
