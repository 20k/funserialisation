#ifndef SERIALISE_HPP_INCLUDED
#define SERIALISE_HPP_INCLUDED

#include <new>
#include <vector>
#include <stdint.h>
#include <iostream>
#include <map>
#include <utility>
#include <type_traits>
#include <fstream>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <SFML/Graphics.hpp>
#include <vec/vec.hpp>
#include <deque>
#include <typeinfo>
#include <optional>

using serialise_host_type = int32_t;
using serialise_owner_type = int32_t;
using serialise_data_type = uint64_t;
using serialise_dirty_type = uint8_t;
using serialise_attention_type = uint8_t;

template<typename T>
void remove_cleanups_from(T& container)
{
    container.erase(std::remove_if(container.begin(),
                                   container.end(),
                                   [&](const auto& v)
                                       { return v.cleanup; }),
                    container.end());
}

template<typename T>
void remove_cleanups_from_set(T& container)
{
    for(auto it = container.begin(); it != container.end();)
    {
        if((*it)->cleanup)
        {
            delete *it;
            it = container.erase(it);
        }
        else
        {
            it++;
        }
    }
}


//#define serialise_owner_type int32_t
//#define serialise_data_type uint64_t

struct serialise;

struct serialise_data
{
    int32_t default_owner = 0;

    std::vector<char> data;

    int internal_counter = 0;
};

struct serialisable;

struct unhandled_types
{
    std::string type_name;
    std::vector<serialisable*> data;
};

struct serialise_data_helper
{
    ///0 = don't follow references unless dirty, 1 = follow (and serialise) references
    static int32_t ref_mode;

    ///0 = partial update, 1 = full
    static int32_t send_mode;

    static inline std::map<serialise_host_type, std::map<serialise_data_type, serialisable*>> host_to_id_to_pointer;

    static std::map<size_t, unhandled_types> type_to_datas;
};

struct serialisable
{
    static serialise_data_type gserialise_id;
    serialise_data_type serialise_id = gserialise_id++;

    ///overload operator = ?
    ///If we duplicate something we don't own, REALLY BAD STUFF will happen
    void get_new_serialise_id()
    {
        serialise_id = gserialise_id++;
    }

    bool owned_by_host = true;
    serialise_host_type host_id = -1;
    serialise_dirty_type dirty = 0;
    serialise_attention_type requires_attention = 0;

    bool force_send = false;

    static inline serialise_data_type explicit_serialise_id = 0;

    //bool owned = true;

    void make_dirty()
    {
        dirty = 1;
    }

    bool handled_by_client = true;

    virtual void do_serialise(serialise& s, bool ser)
    {

    }

    void explicit_register()
    {
        host_id = -2;
        serialise_id = explicit_serialise_id++;
    }

    ///the reason why we crash at outro is, i believe, static initialisation fiasco
    virtual ~serialisable()
    {
        serialise_data_helper::host_to_id_to_pointer[host_id][serialise_id] = nullptr;

        ///remove duplicates
        /*for(auto& i : serialise_data_helper::host_to_id_to_pointer)
        {
            for(auto it = i.second.begin(); it != i.second.end();)
            {
                if(it->second == this)
                    it = i.second.erase(it);
                else
                    it++;
            }
        }*/
    }
};

struct serialise;

template<typename T, typename = std::enable_if_t<!std::is_base_of_v<serialisable, T> && std::is_standard_layout_v<std::remove_reference_t<T>>>>
inline
void lowest_add(T& v, serialise& s, std::vector<char>& data)
{
    char* pv = (char*)&v;

    int ds = data.size();

    data.resize(ds + sizeof(T));

    for(int i=0; i<sizeof(T); i++)
    {
        data[ds + i] = pv[i];
    }
}

template<typename T, typename = std::enable_if_t<!std::is_base_of_v<serialisable, T> && std::is_standard_layout_v<std::remove_reference_t<T>>>>
inline
void lowest_get(T& v, serialise& s, int& internal_counter, std::vector<char>& data)
{
    int prev = internal_counter;

    internal_counter += sizeof(T);

    if(internal_counter > (int)data.size())
    {
        std::cout << "Error, invalid bytefetch low " << typeid(T).name() << std::endl;

        v = T();

        return;
    }

    v = *std::launder((T*)&data[prev]);
}

inline
void lowest_add(serialisable& v, serialise& s, std::vector<char>& data)
{
    v.do_serialise(s, true);
}

inline
void lowest_get(serialisable& v, serialise& s, int& internal_counter, std::vector<char>& data)
{
    v.do_serialise(s, false);
}

template<typename T>
struct serialise_helper
{
    void add(T& v, serialise_data& s)
    {
        lowest_add(v, reinterpret_cast<serialise&>(s), s.data);
    }

    void get(T& v, serialise_data& s)
    {
        lowest_get(v, reinterpret_cast<serialise&>(s), s.internal_counter, s.data);
    }
};

template<typename T>
struct serialise_helper<T*>
{
    void add(T* v, serialise_data& s, bool force = false)
    {
        serialise_helper<serialise_owner_type> helper_owner_id;
        serialise_helper<serialise_data_type> helper1;
        serialise_helper<serialise_dirty_type> helper_dirty;
        serialise_helper<serialise_attention_type> helper_attention;

        if(v == nullptr)
        {
            serialise_owner_type bad_owner = -2;
            helper_owner_id.add(bad_owner, s);

            return;
        }


        if(v->host_id == -1)
        {
            v->host_id = s.default_owner;
        }

        ///we need to clear this every disk save atm
        auto last_ptr = serialise_data_helper::host_to_id_to_pointer[v->host_id][v->serialise_id];

        ///this is fairly expensive
        serialise_data_helper::host_to_id_to_pointer[v->host_id][v->serialise_id] = v;

        helper_owner_id.add(v->host_id, s);
        helper1.add(v->serialise_id, s);
        helper_dirty.add(v->dirty, s);
        helper_attention.add(v->requires_attention, s);

        bool dirty = v->dirty;

        v->dirty = false;

        bool follow_references = serialise_data_helper::ref_mode == 1;

        auto old_send_mode = serialise_data_helper::send_mode;

        if(dirty)
        {
            serialise_data_helper::send_mode = 1;
        }

        ///if dirty, we want to serialise in mode 1, but still not follow references i think (unless dirty)
        ///mode 2?

        bool did_serialise = false;

        ///we're writing out this element for the first time
        if(last_ptr == nullptr && follow_references)
        {
            did_serialise = true;

            v->do_serialise(reinterpret_cast<serialise&>(s), true);
        }

        if(serialise_data_helper::ref_mode == 0 && force && !did_serialise)
        {
            did_serialise = true;

            v->do_serialise(reinterpret_cast<serialise&>(s), true);
        }

        if(!did_serialise && dirty)
        {
            did_serialise = true;
            v->do_serialise(reinterpret_cast<serialise&>(s), true);
        }

        if(dirty && did_serialise)
        {
            //std::cout << "dirty send " << typeid(v).name() << std::endl;
        }

        if(dirty)
        {
            serialise_data_helper::send_mode = old_send_mode;
        }
    }

    ///ok. So. we're specialsied on a pointer which means we're requesting a pointer
    ///but in reality when we serialise, we're going to get our 64bit id followed by the data of the pointer

    void get(T*& v, serialise_data& s, bool force = false)
    {
        serialise_helper<serialise_owner_type> helper_owner_id;

        serialise_owner_type host_id;
        helper_owner_id.get(host_id, s);

        if(host_id == -2)
        {
            v = nullptr;
            return;
        }

        serialise_data_type serialise_id;

        serialise_helper<serialise_data_type> helper1;
        helper1.get(serialise_id, s);

        serialise_dirty_type dirty;

        serialise_helper<serialise_dirty_type> helper_dirty;
        helper_dirty.get(dirty, s);

        serialise_attention_type requires_attention;

        serialise_helper<serialise_attention_type> helper_requires;
        helper_requires.get(requires_attention, s);

        T* ptr = (T*)serialise_data_helper::host_to_id_to_pointer[host_id][serialise_id];

        bool was_nullptr = ptr == nullptr;

        if(was_nullptr)
        {
            ptr = new T();

            //std::cout << typeid(T).name() << " " << ptr << std::endl;

            serialise_data_helper::host_to_id_to_pointer[host_id][serialise_id] = ptr;
        }

        ptr->dirty = false;
        ptr->requires_attention = requires_attention;

        //serialise_helper<T> data_fetcher;
        //*ptr = data_fetcher.get(internal_counter);

        bool did_serialise = false;

        auto old_send_mode = serialise_data_helper::send_mode;

        if(dirty)
        {
            serialise_data_helper::send_mode = 1;
        }

        bool follow_references = serialise_data_helper::ref_mode == 1;

        if(was_nullptr)
        {
            ptr->handled_by_client = false;
            ptr->owned_by_host = false;
            ptr->host_id = host_id;
            ptr->serialise_id = serialise_id;

            ///we're reading this element for the first time
            if(follow_references)
            {
                did_serialise = true;
                ptr->do_serialise(reinterpret_cast<serialise&>(s), false);
            }
        }

        if(serialise_data_helper::ref_mode == 0 && force && !did_serialise)
        {
            did_serialise = true;
            ptr->do_serialise(reinterpret_cast<serialise&>(s), false);
        }

        if(dirty && !did_serialise)
        {
            did_serialise = true;
            ptr->do_serialise(reinterpret_cast<serialise&>(s), false);
        }

        if(dirty && did_serialise)
        {
            //std::cout << "dirty recv " << typeid(ptr).name() << std::endl;
        }

        if(!ptr->handled_by_client && was_nullptr)
        {
            serialise_data_helper::type_to_datas[typeid(ptr).hash_code()].data.push_back(ptr);
            serialise_data_helper::type_to_datas[typeid(ptr).hash_code()].type_name = typeid(ptr).name();
        }

        if(dirty)
        {
            serialise_data_helper::send_mode = old_send_mode;
        }

        v = ptr;
    }
};

template<typename T>
struct serialise_helper<std::vector<T>>
{
    void add(std::vector<T>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;

        int32_t len = v.size();
        helper.add(len, s);

        for(uint32_t i=0; i<v.size(); i++)
        {
            serialise_helper<T> helper;
            helper.add(v[i], s);
        }
    }

    void get(std::vector<T>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;
        int32_t length;
        helper.get(length, s);

        if(length == 0)
            return;

        //v.reserve(length);
        v.resize(length);

        for(int i=0; i<length; i++)
        {
            serialise_helper<T> type;

            type.get(v[i], s);
        }
    }
};

template<typename T>
struct serialise_helper<std::deque<T>>
{
    void add(std::deque<T>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;

        int32_t len = v.size();
        helper.add(len, s);

        for(uint32_t i=0; i<v.size(); i++)
        {
            serialise_helper<T> helper;
            helper.add(v[i], s);
        }
    }

    void get(std::deque<T>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;
        int32_t length;
        helper.get(length, s);

        v.resize(length);

        for(int i=0; i<length; i++)
        {
            serialise_helper<T> type;

            type.get(v[i], s);
        }
    }
};

template<typename T, typename U>
struct serialise_helper<std::map<T, U>>
{
    void add(std::map<T, U>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;

        int32_t len = v.size();
        helper.add(len, s);

        for(auto& i : v)
        {
            serialise_helper<T> h1;
            serialise_helper<U> h2;

            T f_id = i.first;

            h1.add(f_id, s);
            h2.add(i.second, s);
        }
    }

    void get(std::map<T, U>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;
        int32_t length;
        helper.get(length, s);

        for(int i=0; i<length; i++)
        {
            T first;
            U second;

            serialise_helper<T> h1;
            serialise_helper<U> h2;

            h1.get(first, s);
            h2.get(second, s);

            v[first] = std::move(second);
        }
    }
};

template<typename T, typename U>
struct serialise_helper<std::unordered_map<T, U>>
{
    void add(std::unordered_map<T, U>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;

        int32_t len = v.size();
        helper.add(len, s);

        for(auto& i : v)
        {
            serialise_helper<T> h1;
            serialise_helper<U> h2;

            T f_id = i.first;

            h1.add(f_id, s);
            h2.add(i.second, s);
        }
    }

    void get(std::unordered_map<T, U>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;
        int32_t length;
        helper.get(length, s);

        for(int i=0; i<length; i++)
        {
            T first;
            U second;

            serialise_helper<T> h1;
            serialise_helper<U> h2;

            h1.get(first, s);
            h2.get(second, s);

            v[first] = std::move(second);
        }
    }
};

template<typename T>
struct serialise_helper<std::set<T>>
{
    void add(std::set<T>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;

        int32_t len = v.size();
        helper.add(len, s);

        for(auto& i : v)
        {
            auto elem = i;

            serialise_helper<T> helper;
            helper.add(elem, s);
        }
    }

    void get(std::set<T>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;
        int32_t length;
        helper.get(length, s);

        for(int i=0; i<length; i++)
        {
            serialise_helper<T> type;

            T t;
            type.get(t, s);

            v.insert(v.end(), std::move(t));
        }
    }
};

template<typename T>
struct serialise_helper<std::unordered_set<T>>
{
    void add(std::unordered_set<T>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;

        int32_t len = v.size();
        helper.add(len, s);

        for(auto& i : v)
        {
            auto elem = i;

            serialise_helper<T> helper;
            helper.add(elem, s);
        }
    }

    void get(std::unordered_set<T>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;
        int32_t length;
        helper.get(length, s);

        for(int i=0; i<length; i++)
        {
            serialise_helper<T> type;

            T t;
            type.get(t, s);

            v.insert(std::move(t));
        }
    }
};

template<>
struct serialise_helper<std::string>
{
    void add(std::string& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;
        int32_t len = v.size();
        helper.add(len, s);

        for(uint32_t i=0; i<v.size(); i++)
        {
            serialise_helper<decltype(v[i])> helper;
            helper.add(v[i], s);
        }
    }

    void get(std::string& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;
        int32_t length;
        helper.get(length, s);

        if(s.internal_counter + length * sizeof(char) > (int)s.data.size())
        {
            std::cout << "Error, invalid bytefetch st " << length << " " << s.data.size() << std::endl;

            v = std::string();

            return;
        }

        if(length == 0)
            return;

        v.reserve(length);

        for(int i=0; i<length; i++)
        {
            serialise_helper<char> type;

            char c;
            type.get(c, s);

            v.push_back(c);
        }
    }
};

template<typename T>
struct serialise_helper<std::optional<T>>
{
    void add(std::optional<T>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;

        int32_t has = v.has_value();

        helper.add(has, s);

        if(has)
        {
            serialise_helper<decltype(v.value())> h2;

            h2.add(*v, s);
        }
    }

    void get(std::optional<T>& v, serialise_data& s)
    {
        serialise_helper<int32_t> helper;

        int32_t has;

        helper.get(has, s);

        if(has)
        {
            T val;

            serialise_helper<decltype(v.value())> h2;

            h2.get(val, s);

            v = std::move(val);
        }
    }
};

template<>
struct serialise_helper<sf::Texture>
{
    void add(sf::Texture& v, serialise_data& s)
    {
        serialise_helper<vec2i> helper;
        vec2i dim = {v.getSize().x, v.getSize().y};
        helper.add(dim, s);

        if(dim.x() == 0 || dim.y() == 0)
            return;

        serialise_helper<char> smooth;
        char is_smooth = (char)v.isSmooth();
        smooth.add(is_smooth, s);

        sf::Image img = v.copyToImage();

        for(int y=0; y<dim.y(); y++)
        {
            for(int x=0; x<dim.x(); x++)
            {
                sf::Color col = img.getPixel(x, y);

                serialise_helper<uint8_t> h1;
                serialise_helper<uint8_t> h2;
                serialise_helper<uint8_t> h3;
                serialise_helper<uint8_t> h4;

                h1.add(col.r, s);
                h2.add(col.g, s);
                h3.add(col.b, s);
                h4.add(col.a, s);
            }
        }
    }

    void get(sf::Texture& v, serialise_data& s)
    {
        serialise_helper<vec2i> helper;

        vec2i dim;
        helper.get(dim, s);

        if(dim.x() == 0 || dim.y() == 0)
            return;

        serialise_helper<char> smooth_helper;
        char smooth;
        smooth_helper.get(smooth, s);

        sf::Image img;
        img.create(dim.x(), dim.y(), (sf::Uint8*)&s.data[s.internal_counter]);

        s.internal_counter += dim.x() * dim.y() * sizeof(sf::Uint8) * 4;

        v.loadFromImage(img);

        v.setSmooth((bool)smooth);
    }
};

template<typename T>
struct serialise_helper_force
{
    void add(T& v, serialise_data& s)
    {
        serialise_helper<T> helper;
        helper.add(v, s);
    }

    void get(T& v, serialise_data& s)
    {
        serialise_helper<T> helper;
        helper.get(v, s);
    }
};

template<typename T>
struct serialise_helper_force<T*>
{
    void add(T* v, serialise_data& s)
    {
        serialise_helper<T*> helper;
        helper.add(v, s, true);
    }

    void get(T*& v, serialise_data& s)
    {
        serialise_helper<T*> helper;
        helper.get(v, s, true);
    }
};

struct serialise : serialise_data
{
    template<typename T>
    void push_back(T& v, bool force = false)
    {
        if(force)
        {
            serialise_helper_force<T> helper;

            helper.add(v, *this);
        }
        else
        {
            serialise_helper<T> helper;

            helper.add(v, *this);
        }
    }

    template<typename T>
    void get_no_clear(T& val, bool force = false)
    {
        if(force)
        {
            serialise_helper_force<T> helper;

            helper.get(val, *this);
        }
        else
        {
            serialise_helper<T> helper;

            helper.get(val, *this);
        }
    }


    ///if pointer, look up in pointer map
    template<typename T>
    void get(T& val, bool force = false)
    {
        val = T();

        return get_no_clear<T>(val, force);
    }

    template<typename T>
    void force_serialise(T& v, bool ser)
    {
        if(ser)
        {
            push_back(v, true);
        }
        else
        {
            get<T>(v, true);
        }
    }

    template<typename T>
    void handle_serialise(T& v, bool ser)
    {
        if(ser)
        {
            push_back(v);
        }
        else
        {
            get<T>(v);
        }
    }

    template<typename T>
    void handle_serialise_no_clear(T& v, bool ser)
    {
        if(ser)
        {
            push_back(v);
        }
        else
        {
            get_no_clear<T>(v);
        }
    }

    ///force only applies to the first pointer we encounter no more
    ///so this is kind of useless for containers
    template<typename T>
    void handle_serialise_no_clear_force(T& v, bool ser)
    {
        if(ser)
        {
            push_back(v, true);
        }
        else
        {
            get_no_clear<T>(v, true);
        }
    }

    bool finished_deserialising()
    {
        return internal_counter >= (int)data.size();
    }

    ///need to use different serialise_data_helper:host_to_id_to_pointer.clear() strategy for network
    ///and disk mode. Ie two separate tables
    void save(const std::string& file)
    {
        serialise_data_helper::host_to_id_to_pointer.clear();

        if(data.size() == 0)
            return;

        auto myfile = std::fstream(file, std::ios::out | std::ios::binary);
        myfile.write((char*)&data[0], (int)data.size());
        myfile.close();
    }

    void load(const std::string& file)
    {
        serialise_data_helper::host_to_id_to_pointer.clear();

        serialise_data_helper::ref_mode = 1;
        serialise_data_helper::send_mode = 1;

        internal_counter = 0;

        auto myfile = std::fstream(file, std::ios::in | std::ios::out | std::ios::binary);

        myfile.seekg (0, myfile.end);
        int length = myfile.tellg();
        myfile.seekg (0, myfile.beg);

        if(length == 0)
            return;

        data.resize(length);

        myfile.read(data.data(), length);
    }
};

void test_serialisation();

#endif // SERIALISE_HPP_INCLUDED
