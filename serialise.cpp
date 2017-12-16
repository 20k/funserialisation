#include "serialise.hpp"

#include <assert.h>

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

        for(int i=0; i<found.size(); i++)
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

        for(int i=0; i<found.size(); i++)
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

        for(int i=0; i<found.size(); i++)
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

        ser.dump_contents();
        ser.handle_data_coding(true);
        ser.dump_contents();
        ser.handle_data_coding(false);
        ser.dump_contents();

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

void serialise::encode_datastream()
{
    std::map<std::string, int> dictionary;
    int dictSize = 256;

    for (int i = 0; i < 256; i++)
        dictionary[std::string(1, i)] = i;

    std::vector<int> out;
    std::string w;

    for(int i=0; i < data.size(); i++)
    {
        char c = data[i];

        std::string wc = w + c;

        if(dictionary.count(wc))
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

    data.clear();

    for(int i : out)
    {
        char* dat = (char*)&i;

        /*for(int kk=0; kk < sizeof(int); kk++)
        {
            data.push_back(dat[kk]);
        }*/

        int start = data.size();

        data.resize(data.size() + sizeof(i));

        memcpy(&data[start], dat, sizeof(int));
    }
}

void serialise::decode_datastream()
{
    int dictSize = 256;
    std::map<int,std::string> dictionary;

    for (int i = 0; i < 256; i++)
        dictionary[i] = std::string(1, i);

    //int* data_begin = (int*)&data[0];
    //int* data_end = ((int*)&data.back()) + 1;

    //char* data_ptr = &data[0];

    int data_offset = 0;

    //std::cout << data.size() % 4 << std::endl;


    std::string w(1, get_from_char(&data[data_offset]));

    data_offset+=4;

    std::string result = w;

    std::string entry;

    for ( ; data_offset < data.size(); data_offset+=4)
    {
        int k = get_from_char(&data[data_offset]);

        if (dictionary.count(k))
            entry = dictionary[k];
        else if (k == dictSize)
            entry = w + w[0];
        else
            throw "Bad compressed k";

        result += entry;

        dictionary[dictSize++] = w + entry[0];

        w = entry;
    }

    data.clear();

    for(auto& i : result)
    {
        char* ptr = (char*)&i;

        for(int kk=0; kk < sizeof(i); kk++)
        {
            data.push_back(ptr[kk]);
        }
    }

    //return result;
}
