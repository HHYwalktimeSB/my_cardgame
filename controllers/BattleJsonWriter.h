#ifndef BATTLE_JSON_WRITER_H_
#define BATTLE_JSON_WRITER_H_

#include<array>
#include<cassert>
#include<charconv>
#include<cstddef>
#include<cstdint>
#include<string>
#include<string_view>
#include<type_traits>
#include<utility>

namespace cardgame
{

class BattleJsonWriter
{
  public:
    explicit BattleJsonWriter(size_t capacity)
    {
        output_.reserve(capacity);
    }

    void beginObject()
    {
        beforeValue();
        output_.push_back('{');
        pushContext(Container::Object);
    }

    void endObject()
    {
        assert(depth_ > 0 && contexts_[depth_ - 1].container == Container::Object);
        assert(!contexts_[depth_ - 1].waitingForValue);
        --depth_;
        output_.push_back('}');
    }

    void beginArray()
    {
        beforeValue();
        output_.push_back('[');
        pushContext(Container::Array);
    }

    void endArray()
    {
        assert(depth_ > 0 && contexts_[depth_ - 1].container == Container::Array);
        --depth_;
        output_.push_back(']');
    }

    void key(std::string_view value)
    {
        assert(depth_ > 0 && contexts_[depth_ - 1].container == Container::Object);
        auto &context = contexts_[depth_ - 1];
        assert(!context.waitingForValue);
        appendSeparator(context);
        appendQuoted(value);
        output_.push_back(':');
        context.waitingForValue = true;
    }

    template<typename Integer,
             typename = std::enable_if_t<
                 std::is_integral_v<Integer> &&
                 !std::is_same_v<std::remove_cv_t<Integer>, bool>>>
    void integer(Integer value)
    {
        beforeValue();
        char buffer[32];
        const auto result = std::to_chars(
            buffer,
            buffer + sizeof(buffer),
            value);
        output_.append(buffer, static_cast<size_t>(result.ptr - buffer));
    }

    void boolean(bool value)
    {
        beforeValue();
        output_ += (value ? "true" : "false");
    }

    void nullValue()
    {
        beforeValue();
        output_ += "null";
    }

    void string(std::string_view value)
    {
        beforeValue();
        appendQuoted(value);
    }

    std::string take()
    {
        assert(depth_ == 0);
        return std::move(output_);
    }

  private:
    enum class Container
    {
        Object,
        Array
    };

    struct Context
    {
        Container container;
        bool first{true};
        bool waitingForValue{false};
    };

    static constexpr size_t maxDepth = 8;

    void pushContext(Container container)
    {
        assert(depth_ < maxDepth);
        contexts_[depth_++] = {container, true, false};
    }

    void beforeValue()
    {
        if(depth_ == 0)return;
        auto &context = contexts_[depth_ - 1];
        if(context.container == Container::Array)
            appendSeparator(context);
        else
        {
            assert(context.waitingForValue);
            context.waitingForValue = false;
        }
    }

    void appendSeparator(Context &context)
    {
        if(context.first)context.first = false;
        else output_.push_back(',');
    }

    void appendQuoted(std::string_view value)
    {
        static constexpr char hex[] = "0123456789abcdef";
        output_.push_back('"');
        for(unsigned char character : value)
        {
            switch(character)
            {
            case '"': output_ += "\\\""; break;
            case '\\': output_ += "\\\\"; break;
            case '\b': output_ += "\\b"; break;
            case '\f': output_ += "\\f"; break;
            case '\n': output_ += "\\n"; break;
            case '\r': output_ += "\\r"; break;
            case '\t': output_ += "\\t"; break;
            default:
                if(character < 0x20)
                {
                    output_ += "\\u00";
                    output_.push_back(hex[character >> 4]);
                    output_.push_back(hex[character & 0x0f]);
                }
                else output_.push_back(static_cast<char>(character));
            }
        }
        output_.push_back('"');
    }

    std::string output_;
    std::array<Context, maxDepth> contexts_{};
    size_t depth_{0};
};

}

#endif
