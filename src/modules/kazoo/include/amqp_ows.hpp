#ifndef __AMQP_OWS_H__
#define __AMQP_OWS_H__

#include "amqp/include/amqpcpp.h"

namespace quotes
{
struct Quoted
{
    bool is_quoted;

    Quoted(bool base = true) : is_quoted(base)
    {
    }

    static int getIWord()
    {
        // call xalloc once to get an index at which we can store data for this
        // manipulator.
        static int iw = std::ios_base::xalloc();
        return iw;
    }

    void apply(std::ostream& os) const
    {
        // store the base value in the manipulator.
        os.iword(getIWord()) = is_quoted;
    }
};

static const Quoted noquotes = Quoted(false);
static const Quoted quoted = Quoted(true);

inline bool IsQuoted(std::ios_base& os)
{
    return os.iword(Quoted::getIWord());
}


// We need this so we can apply our custom stream manipulator to the stream.
std::ostream& operator<<(std::ostream& os, const Quoted& bm)
{
    bm.apply(os);
    return os;
}

}


namespace AMQP
{


class owstream : public std::ostream
{
public:
    owstream(std::streambuf* sb)
        : std::ostream(sb)
    {}

    // Template based operator...ohhhhhh ahhhhh.
    template <typename T>
    friend owstream& operator<<(owstream&, const T&);

    // Additional overload to handle ostream specific io manipulators
    friend owstream& operator<<(owstream&, std::ostream& (*)(std::ostream&));

    // Accessor function to get a reference to the ostream
    std::ostream& get_ostream() { return *this; }
};

//  overload for std::ostream specific io manipulators
inline owstream& operator<<(owstream& out, std::ostream& (*func)(std::ostream&))
{
	static_cast<std::ostream&>(out) << func;
	return out;
}

typedef std::map<std::string, std::shared_ptr<Field> > FieldMap;
typedef std::vector<std::shared_ptr<Field>> FieldArray;


inline owstream& operator<<(owstream& out, const FieldArray& value);
//template <> inline owstream& operator<<(owstream& out, const FieldMap& value);
inline owstream& operator<<(owstream& out, const Field& value);
//template <> inline owstream& operator<<(owstream& out, const Table& value);
//template <> inline owstream& operator<<(owstream& out, const FieldProxy<Table, std::string>& value);
//template <> inline owstream& operator<<(owstream& out, const FieldProxy<Array, uint8_t>& value);


template <typename T>
inline owstream& operator<<(owstream& out, const T& value)
{
	static_cast<std::ostream&>(out) << value;
	return out;
}

template <>
inline owstream& operator << (owstream& out, const std::shared_ptr<Field>& v)
{
	return out << *v;
}

template <>
inline owstream& operator<<(owstream& out, const FieldMap& value)
{
	out << "{";
    bool first = true;
    for (auto &iter : value)
    {
        // split with comma
        if (!first) out << ", ";

        // show output
        out << "\"" << iter.first <<"\": " << *iter.second;

        // no longer first iter
        first = false;
    }
    return out << "}";
}

template <>
inline owstream& operator<<(owstream& out, const Table& value)
{
	return out << value.Fields();
}


template <>
inline owstream& operator<<(owstream& out, const Field& value)
{
    const bool quoted = out.iword(quotes::Quoted::getIWord());
    std::cerr << "QUOTES ARE " << quoted << std::endl;
    switch (value.typeID())
    {
        case 't':   return out << (static_cast<BooleanSet>(value).value() ? "true" : "false");
        case 'b':   return out << static_cast<Octet>(value).value();
        case 'B':   return out << static_cast<UOctet>(value).value();
        case 'U':   return out << static_cast<Short>(value).value();
        case 'u':   return out << static_cast<UShort>(value).value();
        case 'I':   return out << static_cast<Long>(value).value();
        case 'i':   return out << static_cast<ULong>(value).value();
        case 'L':   return out << static_cast<LongLong>(value).value();
        case 'l':   return out << static_cast<ULongLong>(value).value();
        case 'f':   return out << static_cast<Float>(value).value();
        case 'd':   return out << static_cast<Double>(value).value();
        case 'D':   return out << static_cast<DecimalField>(value).value();
        case 's':   return out << (quoted ? "\"" : "") << static_cast<ShortString>(value).value() << (quoted ? "\"" : "");
        case 'S':   return out << (quoted ? "\"" : "") << static_cast<LongString>(value).value() << (quoted ? "\"" : "");
        case 'A':   return out << static_cast<Array>(value).Fields();
        case 'T':   return out << static_cast<Timestamp>(value).value();
        case 'F':   return out << static_cast<Table>(value).Fields();
        default:    return out;
    }
}

template <>
inline owstream& operator<<(owstream& out, const FieldArray& value)
{
    out << "[";
    bool first = true;
    for (auto ii = value.begin(); ii != value.end(); ++ii)
    {
    	if(!first) out << ",";
        out  << *ii;
        first = false;
    }
    out << "]";
    return out;
}

template <>
inline owstream& operator<<(owstream& out, const FieldProxy<Table, std::string>& value)
{
    return out << value.get();
}

template <>
inline owstream& operator<<(owstream& out, const FieldProxy<Array, uint8_t>& value)
{
    return out << value.get();
};


inline owstream& operator<<(owstream& out, const Field& value)
{
    const bool quoted = quotes::IsQuoted(static_cast<std::ios_base&>(out));
    switch (value.typeID())
    {
        case 't':   return out << (static_cast<BooleanSet>(value).value() ? "true" : "false");
        case 'b':   return out << static_cast<Octet>(value).value();
        case 'B':   return out << static_cast<UOctet>(value).value();
        case 'U':   return out << static_cast<Short>(value).value();
        case 'u':   return out << static_cast<UShort>(value).value();
        case 'I':   return out << static_cast<Long>(value).value();
        case 'i':   return out << static_cast<ULong>(value).value();
        case 'L':   return out << static_cast<LongLong>(value).value();
        case 'l':   return out << static_cast<ULongLong>(value).value();
        case 'f':   return out << static_cast<Float>(value).value();
        case 'd':   return out << static_cast<Double>(value).value();
        case 'D':   return out << static_cast<DecimalField>(value).value();
        case 's':   return out << (quoted ? "\"" : "") << static_cast<ShortString>(value).value() << (quoted ? "\"" : "");
        case 'S':   return out << (quoted ? "\"" : "") << static_cast<LongString>(value).value() << (quoted ? "\"" : "");
        case 'A':   return out << static_cast<Array>(value).Fields();
        case 'T':   return out << static_cast<Timestamp>(value).value();
        case 'F':   return out << static_cast<Table>(value).Fields();
        default:    return out;
    }
}

inline owstream& operator<<(owstream& out, const FieldArray& value)
{
    out << "[";
    bool first = true;
    for (auto ii = value.begin(); ii != value.end(); ++ii)
    {
    	if(!first) out << ",";
        out  << *ii;
        first = false;
    }
    out << "]";
    return out;
}

};

#endif
