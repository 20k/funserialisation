#include "serialise.hpp"

#include <assert.h>
#include <thread>

uint64_t serialisable::gserialise_id;

int32_t serialise_data_helper::ref_mode = 1;
int32_t serialise_data_helper::send_mode = 1;
//std::map<serialise_host_type, std::map<serialise_data_type, serialisable*>> serialise_data_helper::host_to_id_to_pointer;
//int serialise_data_helper::pass = 0;
std::map<size_t, unhandled_types> serialise_data_helper::type_to_datas;

struct test_object : serialisable
{
    float v1 = 12.f;
    float v2 = 54.f;

    virtual void do_serialise(serialise& s, bool ser)
    {
        s.handle_serialise(v1, ser);
        s.handle_serialise(v2, ser);
    }

    virtual ~test_object(){}
};

void test_serialisation()
{
    serialise_data_helper::ref_mode = 1;
    serialise_data_helper::send_mode = 1;

    ///test serialisation sanity for basic data types
    {
        uint64_t val = 5343424;

        serialise ser;
        ser.handle_serialise(val, true);

        uint64_t rval;

        ser.handle_serialise(rval, false);

        assert(rval == val);
    }

    {
        test_object obj;

        obj.v1 = -99;

        serialise ser;
        ser.handle_serialise(obj, true);

        test_object ret;

        ser.handle_serialise(ret, false);

        assert(obj.v1 == ret.v1);
    }

    ///test receiving new data
    {
        test_object* test = new test_object;

        serialise s2;

        s2.handle_serialise(test, true);


        ///emulate network receive
        serialise_data_helper::host_to_id_to_pointer[test->host_id][test->serialise_id] = nullptr;

        test_object* received;

        s2.handle_serialise(received, false);

        assert(received != nullptr);

        assert(received->handled_by_client == false);
        assert(received->owned_by_host == false);

        assert(received->v1 == test->v1);
        assert(received->v2 == test->v2);

        delete test;
        delete received;
    }

    ///test receiving data from/about ourselves to check ownership semantics work
    /*{
        test_object* test = new test_object;

        serialise s2;

        s2.handle_serialise(test, true);

        test_object* received;

        s2.handle_serialise(received, false);

        assert(received != nullptr);

        assert(received->handled_by_client == true);
        assert(received->owned_by_host == true);

        assert(received->v1 == test->v1);
        assert(received->v2 == test->v2);

        assert(test == received);

        delete test;
    }*/


    ///ok. Final test:
    ///Can we ping data from a to b, modify b, then ping it back to a
    /*{
        test_object* test = new test_object;

        serialise ser;
        ser.handle_serialise(test, true);

        serialise_data_helper::owner_to_id_to_pointer.clear();

        test_object* received;
        ser.handle_serialise(received, false);

        received->v1 = 99;
        received->v2 = 22;

        serialise s2;
        s2.handle_serialise(received, true);

        serialise_data_helper::owner_to_id_to_pointer[test->owner_id][test->serialise_id] = test;

        s2.handle_serialise(test, false);

        assert(test->v1 == received->v1);
        assert(test->v2 == received->v2);
    }*/

    {
        std::vector<test_object> objects;

        objects.resize(5);

        objects[1].v1 = 3434;

        serialise ser;
        ser.handle_serialise(objects, true);

        std::vector<test_object> found;

        ser.handle_serialise(found, false);

        assert(found.size() == objects.size());

        for(int i=0; i<(int)found.size(); i++)
        {
            assert(objects[i].v1 == found[i].v1);
            assert(objects[i].v2 == found[i].v2);
        }
    }

    {
        std::deque<test_object> objects;

        objects.resize(5);

        objects[1].v1 = 3434;

        serialise ser;
        ser.handle_serialise(objects, true);

        std::deque<test_object> found;

        ser.handle_serialise(found, false);

        assert(found.size() == objects.size());

        for(int i=0; i<(int)found.size(); i++)
        {
            assert(objects[i].v1 == found[i].v1);
            assert(objects[i].v2 == found[i].v2);
        }
    }

    {
        std::vector<test_object*> objects;
        objects.resize(5);

        for(int i=0; i<5; i++)
        {
            objects[i] = new test_object();
        }

        objects[2]->v1 = 993939;

        serialise ser;
        ser.handle_serialise(objects, true);

        serialise_data_helper::host_to_id_to_pointer.clear();

        std::vector<test_object*> found;

        ser.handle_serialise(found, false);

        assert(found.size() == objects.size());

        for(int i=0; i<(int)found.size(); i++)
        {
            assert(objects[i]->v1 == found[i]->v1);
            assert(objects[i]->v2 == found[i]->v2);

            assert(found[i]->handled_by_client == false);

            delete found[i];
        }
    }

    {
        std::string name = "oooh weee\ndfdf0";

        serialise ser;
        ser.handle_serialise(name, true);

        std::string ret;

        ser.handle_serialise(ret, false);

        assert(ret == name);
    }

    {
        std::vector<test_object*> objects;

        objects.resize(5);

        serialise ser;
        ser.handle_serialise(objects, true);

        serialise_data_helper::host_to_id_to_pointer.clear();

        std::vector<test_object*> test;

        ser.handle_serialise(test, false);

        assert(test.size() == objects.size());
    }

    {
        std::map<int, float> test_map;

        test_map[0] = 12;
        test_map[10] = 3;
        test_map[2] = -323;

        serialise ser;
        ser.handle_serialise(test_map, true);

        std::map<int, float> received_map;

        ser.handle_serialise(received_map, false);

        assert(received_map.size() == test_map.size());

        for(auto& i : received_map)
        {
            assert(i.second == test_map[i.first]);
        }
    }

    {
        std::unordered_map<int, float> test_map;

        test_map[0] = 12;
        test_map[10] = 3;
        test_map[2] = -323;

        serialise ser;
        ser.handle_serialise(test_map, true);

        std::unordered_map<int, float> received_map;

        ser.handle_serialise(received_map, false);

        assert(received_map.size() == test_map.size());

        for(auto& i : received_map)
        {
            assert(i.second == test_map[i.first]);
        }
    }

    {
        std::set<int> test_map;

        test_map.insert(23);
        test_map.insert(44);
        test_map.insert(66);
        test_map.insert(-12);

        serialise ser;
        ser.handle_serialise(test_map, true);

        std::set<int> received_map;

        ser.handle_serialise(received_map, false);

        assert(received_map.size() == test_map.size());

        for(auto& i : received_map)
        {
            assert(received_map.find(i) != received_map.end());
        }
    }

    {
        std::unordered_set<int> test_map;

        test_map.insert(23);
        test_map.insert(44);
        test_map.insert(66);
        test_map.insert(-12);

        serialise ser;
        ser.handle_serialise(test_map, true);

        std::unordered_set<int> received_map;

        ser.handle_serialise(received_map, false);

        assert(received_map.size() == test_map.size());

        for(auto& i : received_map)
        {
            assert(received_map.find(i) != received_map.end());
        }
    }

    {
        std::optional<int> test;

        serialise ser;
        ser.handle_serialise(test, true);

        std::optional<int> recv;

        ser.handle_serialise(recv, false);

        assert(recv.has_value() == false);
    }

    {
        std::optional<int> test = 12;

        serialise ser;
        ser.handle_serialise(test, true);

        std::optional<int> recv;

        ser.handle_serialise(recv, false);

        assert(recv.has_value() == true);

        assert(*recv == 12);
    }

    {
        int val1 = 32;
        int val2 = 43;
        int val3 = 545;

        serialise ser;
        ser.handle_serialise(val1, true);
        ser.handle_serialise(val2, true);
        ser.handle_serialise(val3, true);

        //ser.dump_contents();
        ser.handle_data_coding(true);
        //ser.dump_contents();
        ser.handle_data_coding(false);
        //ser.dump_contents();

        int nv1 = 32;
        int nv2 = 43;
        int nv3 = 545;

        ser.handle_serialise(nv1, false);
        ser.handle_serialise(nv2, false);
        ser.handle_serialise(nv3, false);

        assert(val1 == nv1);
        assert(val2 == nv2);
        assert(val3 == nv3);
    }

    ///ensure tests don't interfere with global state. It shouldn't affect it even without this call
    ///but it may make debugging easier
    serialise_data_helper::host_to_id_to_pointer.clear();
}

std::vector<int> encode_partial(int start, int length, const std::vector<char>& data)
{
    std::map<std::string, int> dictionary;

    int dictSize = 256;

    for (int i = 0; i < 256; i++)
        dictionary[std::string(1, i)] = i;

    std::vector<int> out;
    std::string w;

    ///hmm, lowering it to 7 makes it compress in a couple of seconds
    ///but reduces data compression quite significantly
    int max_length = 50;

    for(int i=start; i < start + length && i < (int)data.size(); i++)
    {
        char c = data[i];

        std::string wc = w + c;

        if((int)wc.size() <= max_length && dictionary.count(wc))
        {
            w = std::move(wc);
        }
        else
        {
            out.push_back(dictionary[w]);

            dictionary[wc] = dictSize++;

            w = std::string(1, c);
        }
    }

    if(!w.empty())
    {
        out.push_back(dictionary[w]);
    }

    out.push_back(-1);

    return out;
}

///client uses thread pool, server uses manual threads
///as it never encodes data
#if defined(NET_CLIENT) && !defined(NO_COMPRESSION) && !defined(SINGLE_THREADED)
#define THREAD_POOL
#endif

#ifdef THREAD_POOL
struct tiny_thread_pool
{
    std::thread threads[4];
    volatile int has_data[4] = {0};

    struct exec_data
    {
        std::vector<std::vector<int>>* partial_data;
        std::vector<char> (*data);
        int max_size = 0;

        void operator = (const exec_data& a) volatile
        {
            partial_data = a.partial_data;
            data = a.data;
            max_size = a.max_size;
        }

        void operator = (volatile exec_data& a) volatile
        {
            partial_data = a.partial_data;
            data = a.data;
            max_size = a.max_size;
        }

        void operator = (volatile exec_data a)
        {
            partial_data = a.partial_data;
            data = a.data;
            max_size = a.max_size;
        }
    };

    volatile exec_data datas[4];

    void assign_work(const exec_data& d1)
    {
        wait();

        for(int i=0; i < 4; i++)
        {
            datas[i] = d1;
        }

        for(int i=0; i < 4; i++)
        {
            has_data[i] = 1;
        }
    }

    void thread_function(int id)
    {
        while(1)
        {
            int value = has_data[id];

            if(value == 1)
            {
                volatile exec_data& dat = datas[id];

                (*dat.partial_data)[id] = encode_partial(id * dat.max_size, dat.max_size, (*dat.data));

                has_data[id] = 0;
            }

            if(value == -1)
            {
                return;
            }

            if(value == -2)
            {
                sf::sleep(sf::milliseconds(1));
                has_data[id] = 0;
            }

            std::this_thread::yield();
        }
    }

    tiny_thread_pool()
    {
        for(int i=0; i < 4; i++)
        {
            threads[i] = std::thread(thread_function, this, i);
        }
    }

    ~tiny_thread_pool()
    {
        for(int i=0; i<4; i++)
        {
            has_data[i] = -1;

            threads[i].join();
        }
    }

    void sleep()
    {
        for(int i=0; i < 4; i++)
        {
            has_data[i] = -2;
        }
    }

    void wait()
    {
        bool finished = false;

        while(!finished)
        {
            bool any_going = false;

            for(int i=0; i < 4; i++)
            {
                if(has_data[i] != 0)
                {
                    any_going = true;
                    break;
                }
            }

            if(!any_going)
                finished = true;
            else
                std::this_thread::yield();
        }

    }
};
#endif

#ifdef THREAD_POOL
inline tiny_thread_pool thread_pool;
#endif

void serialise::sleep_thread_pool()
{
    #ifdef THREAD_POOL
    thread_pool.sleep();
    #endif
}

#define SPLITS 4

void serialise::encode_datastream()
{
    #ifdef NO_COMPRESSION
    return;
    #endif // NO_COMPRESSION

    //sf::Clock clk;

    ///720k entries
    std::vector<int> out;

    constexpr int splits = SPLITS;
    int max_size = ceil((double)data.size() / splits);

    #if defined(NET_SERVER) && !defined(NO_COMPRESSION) && !defined(SINGLE_THREADED)
    #define MANUAL_THREADS
    #endif

    #ifdef MANUAL_THREADS
    std::thread threads[splits];
    #endif

    std::vector<std::vector<int>> partial_data;
    partial_data.resize(splits);

    for(int i=0; i < splits; i++)
    {
        #ifdef MANUAL_THREADS
        threads[i] = std::thread([i, this, &partial_data, max_size](){partial_data[i] = encode_partial(i * max_size, max_size, data);});
        #endif // MANUAL_THREADS
    }

    #ifdef THREAD_POOL
    tiny_thread_pool::exec_data dat;
    dat.max_size = max_size;
    dat.partial_data = &partial_data;
    dat.data = &data;

    thread_pool.assign_work(dat);
    #endif

    for(int i=0; i < splits; i++)
    {
        #ifdef MANUAL_THREADS
        threads[i].join();
        #endif
    }

    #ifdef THREAD_POOL
    thread_pool.wait();
    #endif

    for(int i=0; i < splits; i++)
    {
        #ifdef SINGLE_THREADED
        auto found = encode_partial(i * max_size, max_size, data);
        #else
        auto found = partial_data[i];
        #endif // SINGLE_THREADED

        out.insert(out.end(), found.begin(), found.end());
    }

    data.clear();

    for(int i : out)
    {
        char* dat = (char*)&i;

        int start = data.size();

        data.resize(data.size() + sizeof(i));

        memcpy(&data[start], dat, sizeof(int));
    }

    //std::cout << clk.getElapsedTime().asMicroseconds() / 1000. / 1000. << " time" << std::endl;
}

std::string decode_partial(std::vector<char>& data, int& in_out_end)
{
    int dictSize = 256;
    std::map<int,std::string> dictionary;

    for (int i = 0; i < 256; i++)
        dictionary[i] = std::string(1, i);

    int data_offset = in_out_end;

    std::string w(1, serialise::get_from_char(&data[data_offset]));

    data_offset+=4;

    std::string result = w;

    std::string entry;

    for ( ; data_offset < (int)data.size(); data_offset+=4)
    {
        int k = serialise::get_from_char(&data[data_offset]);

        if(k == -1)
        {
            in_out_end = data_offset + 4;

            return result;
        }

        if (dictionary.count(k))
            entry = dictionary[k];
        else if (k == dictSize)
            entry = w + w[0];
        else
        {
            std::cout << "ERR ERR ERR\n";
        }

        result += entry;

        dictionary[dictSize++] = w + entry[0];

        w = entry;
    }

    return result;
}

void serialise::decode_datastream()
{
    #ifdef NO_COMPRESSION
    return;
    #endif // NO_COMPRESSION

    std::string result;

    int splits = SPLITS;

    int cur_start = 0;

    for(int i=0; i < splits; i++)
    {
        result += decode_partial(data, cur_start);
    }

    data.clear();

    for(auto& i : result)
    {
        char* ptr = (char*)&i;

        for(int kk=0; kk < (int)sizeof(i); kk++)
        {
            data.push_back(ptr[kk]);
        }
    }
}

#ifndef NET_CLIENT
#ifndef NET_SERVER
#error "Must define one of the above"
#endif
#endif // NET_CLIENT
