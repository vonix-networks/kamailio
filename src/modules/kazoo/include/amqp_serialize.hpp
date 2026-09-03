#ifndef __AMQP_SERIALIZE_H__
#define __AMQP_SERIALIZE_H__

#include "json.hpp"
#include "amqp/include/amqpcpp.h"


namespace AMQP
{

using json = nlohmann::json;

typedef std::map<std::string, std::shared_ptr<Field> > FieldMap;
typedef std::vector<std::shared_ptr<Field>> FieldArray;


void to_json(json& j, const Array& p);
void from_json(const json& j, Array& p);

void to_json(json& j, const Table& p);
void from_json(const json& j, Table& p);

void to_json(json& j, const std::shared_ptr<Field>& p);

void to_json(json& j, const Field& p);

}

#endif

