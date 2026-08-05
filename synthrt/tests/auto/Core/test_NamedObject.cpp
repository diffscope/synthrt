#include <string>
#include <vector>

#include <synthrt/Core/NamedObject.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using srt::NamedObject;
using srt::NO;
using srt::ObjectPool;
using srt::UNO;

namespace {

    /// Says when it goes, so a test can tell whether removing an object destroyed it or only let
    /// go of it.
    class Tracked : public NamedObject {
    public:
        explicit Tracked(std::string name, int *liveCount)
            : NamedObject(std::move(name)), _liveCount(liveCount) {
            ++*_liveCount;
        }
        ~Tracked() override {
            --*_liveCount;
        }

    private:
        int *_liveCount;
    };

    /// Records every notification, in the order it arrived.
    class WatchedPool : public ObjectPool {
    public:
        std::vector<std::string> log;

    protected:
        void sharedObjectAdded(std::string_view id, const NO<NamedObject> &obj) override {
            log.push_back("+shared " + std::string(id) + "/" + obj->objectName());
        }
        void aboutToRemoveSharedObject(std::string_view id, const NO<NamedObject> &obj) override {
            log.push_back("-shared " + std::string(id) + "/" + obj->objectName());
        }
        void uniqueObjectAdded(std::string_view id, NamedObject *obj) override {
            log.push_back("+unique " + std::string(id) + "/" + obj->objectName());
        }
        void aboutToRemoveUniqueObject(std::string_view id, NamedObject *obj) override {
            log.push_back("-unique " + std::string(id) + "/" + obj->objectName());
        }
    };

}

BOOST_AUTO_TEST_SUITE(test_NamedObject)

BOOST_AUTO_TEST_CASE(test_ObjectPool_Shared) {
    int live = 0;
    ObjectPool pool;

    auto a = NO<Tracked>::create("a", &live);
    auto b = NO<Tracked>::create("b", &live);
    pool.addSharedObject("id", a);
    pool.addSharedObject("id", b);

    BOOST_CHECK(pool.getSharedObjects("id").size() == 2);
    BOOST_CHECK(pool.getFirstSharedObject("id") == a);
    BOOST_CHECK(pool.allSharedObjects().size() == 2);

    // Registration is in order, and registering the same object again is not a second entry.
    pool.addSharedObject("id", a);
    BOOST_CHECK(pool.getSharedObjects("id").size() == 2);

    // Nothing is there under another identifier, and nothing is there for an empty one.
    BOOST_CHECK(pool.getSharedObjects("other").empty());
    BOOST_CHECK(pool.getFirstSharedObject("other") == nullptr);

    // Removing lets go of the pool's reference, and the caller's keeps the object alive.
    BOOST_CHECK(live == 2);
    pool.removeSharedObject("id", a.get());
    BOOST_CHECK(live == 2);
    BOOST_CHECK(pool.getSharedObjects("id").size() == 1);
    BOOST_CHECK(pool.getFirstSharedObject("id") == b);

    pool.removeSharedObjects("id");
    BOOST_CHECK(pool.getSharedObjects("id").empty());
    BOOST_CHECK(live == 2);
}

BOOST_AUTO_TEST_CASE(test_ObjectPool_Unique) {
    int live = 0;
    ObjectPool pool;

    pool.addUniqueObject("id", UNO<Tracked>::create("a", &live));
    pool.addUniqueObject("id", UNO<Tracked>::create("b", &live));
    BOOST_CHECK(live == 2);

    auto objects = pool.getUniqueObjects("id");
    BOOST_REQUIRE(objects.size() == 2);
    BOOST_CHECK(objects[0]->objectName() == "a");
    BOOST_CHECK(objects[1]->objectName() == "b");
    BOOST_CHECK(pool.getFirstUniqueObject("id") == objects[0]);
    BOOST_CHECK(pool.allUniqueObjects().size() == 2);

    BOOST_CHECK(pool.getUniqueObjects("other").empty());
    BOOST_CHECK(pool.getFirstUniqueObject("other") == nullptr);

    // Removing one destroys it, since the pool was the only thing holding it.
    pool.removeUniqueObject("id", objects[0]);
    BOOST_CHECK(live == 1);
    BOOST_CHECK(pool.getUniqueObjects("id").size() == 1);

    pool.removeUniqueObjects("id");
    BOOST_CHECK(live == 0);
    BOOST_CHECK(pool.getUniqueObjects("id").empty());
}

// Everything left in the pool goes when the pool does.
BOOST_AUTO_TEST_CASE(test_ObjectPool_Destruction) {
    int live = 0;
    {
        ObjectPool pool;
        pool.addUniqueObject("id", UNO<Tracked>::create("owned", &live));
        BOOST_CHECK(live == 1);
    }
    BOOST_CHECK(live == 0);
}

// The two sets are separate. An identifier registered in one is not found in the other, which is
// the point of having them apart rather than one set with a flag.
BOOST_AUTO_TEST_CASE(test_ObjectPool_SetsAreIndependent) {
    int live = 0;
    ObjectPool pool;

    auto shared = NO<Tracked>::create("shared", &live);
    pool.addSharedObject("driver", shared);
    pool.addUniqueObject("driver", UNO<Tracked>::create("owned", &live));

    BOOST_CHECK(pool.getSharedObjects("driver").size() == 1);
    BOOST_CHECK(pool.getUniqueObjects("driver").size() == 1);
    BOOST_CHECK(pool.getFirstSharedObject("driver")->objectName() == "shared");
    BOOST_CHECK(pool.getFirstUniqueObject("driver")->objectName() == "owned");

    // Clearing one leaves the other alone.
    pool.removeAllUniqueObjects();
    BOOST_CHECK(pool.getUniqueObjects("driver").empty());
    BOOST_CHECK(pool.getSharedObjects("driver").size() == 1);
    BOOST_CHECK(live == 1);

    pool.removeAllSharedObjects();
    BOOST_CHECK(pool.getSharedObjects("driver").empty());
}

// A null goes nowhere, rather than becoming an entry that answers nothing.
BOOST_AUTO_TEST_CASE(test_ObjectPool_Null) {
    ObjectPool pool;
    pool.addSharedObject("id", NO<NamedObject>());
    pool.addUniqueObject("id", UNO<NamedObject>());
    BOOST_CHECK(pool.getSharedObjects("id").empty());
    BOOST_CHECK(pool.getUniqueObjects("id").empty());
    BOOST_CHECK(pool.allSharedObjects().empty());
    BOOST_CHECK(pool.allUniqueObjects().empty());
}

BOOST_AUTO_TEST_CASE(test_ObjectPool_Notifications) {
    int live = 0;
    WatchedPool pool;

    auto shared = NO<Tracked>::create("s", &live);
    pool.addSharedObject("id", shared);
    pool.addUniqueObject("id", UNO<Tracked>::create("u1", &live));
    pool.addUniqueObject("id", UNO<Tracked>::create("u2", &live));

    // Adding the same shared object again is not a second notification.
    pool.addSharedObject("id", shared);

    BOOST_REQUIRE(pool.log.size() == 3);
    BOOST_CHECK(pool.log[0] == "+shared id/s");
    BOOST_CHECK(pool.log[1] == "+unique id/u1");
    BOOST_CHECK(pool.log[2] == "+unique id/u2");

    // Clearing an identifier tears down what was registered later first.
    pool.log.clear();
    pool.removeUniqueObjects("id");
    BOOST_REQUIRE(pool.log.size() == 2);
    BOOST_CHECK(pool.log[0] == "-unique id/u2");
    BOOST_CHECK(pool.log[1] == "-unique id/u1");

    pool.log.clear();
    pool.removeSharedObject("id", shared.get());
    BOOST_REQUIRE(pool.log.size() == 1);
    BOOST_CHECK(pool.log[0] == "-shared id/s");
}

BOOST_AUTO_TEST_CASE(test_NamedObject_Name) {
    NamedObject plain;
    BOOST_CHECK(plain.objectName().empty());

    plain.setObjectName("later");
    BOOST_CHECK(plain.objectName() == "later");

    NamedObject named("given");
    BOOST_CHECK(named.objectName() == "given");
}

BOOST_AUTO_TEST_CASE(test_NamedObject_Property) {
    NamedObject obj;

    // A name that was never set reads as an empty value rather than throwing or inserting one.
    BOOST_CHECK(!obj.property("absent").has_value());

    obj.setProperty("count", 7);
    BOOST_REQUIRE(obj.property("count").has_value());
    BOOST_CHECK(std::any_cast<int>(obj.property("count")) == 7);

    // Setting the same name again replaces what was there, type and all.
    obj.setProperty("count", std::string("seven"));
    BOOST_CHECK(std::any_cast<std::string>(obj.property("count")) == "seven");

    // Asking for the wrong type is the caller's problem, and the pointer form says so quietly.
    BOOST_CHECK(std::any_cast<int>(&obj.property("count")) == nullptr);
}

BOOST_AUTO_TEST_CASE(test_NO) {
    int live = 0;
    {
        auto tracked = NO<Tracked>::create("t", &live);
        BOOST_REQUIRE(tracked != nullptr);
        BOOST_CHECK(live == 1);
        BOOST_CHECK(tracked->objectName() == "t");

        // Sharing is what it is for, and the object outlives any one of the references.
        NO<NamedObject> base = tracked;
        BOOST_CHECK(base.use_count() == 2);
        BOOST_CHECK(base.as<Tracked>() == tracked);

        tracked = {};
        BOOST_CHECK(live == 1);
    }
    BOOST_CHECK(live == 0);
}

BOOST_AUTO_TEST_CASE(test_UNO) {
    int live = 0;
    {
        auto owned = UNO<Tracked>::create("t", &live);
        BOOST_REQUIRE(owned != nullptr);
        BOOST_CHECK(live == 1);

        // as() hands back a pointer and leaves ownership where it was, unlike NO::as().
        NamedObject *borrowed = owned.as<NamedObject>();
        BOOST_CHECK(borrowed == owned.get());
        BOOST_CHECK(owned != nullptr);
    }
    BOOST_CHECK(live == 0);

    // Moving into an NO is how a factory hands out sole ownership without deciding for its caller
    // whether the object will be shared.
    {
        auto owned = UNO<Tracked>::create("t", &live);
        auto raw = owned.get();
        NO<NamedObject> shared = std::move(owned);
        BOOST_CHECK(owned == nullptr);
        BOOST_CHECK(shared.get() == raw);
        BOOST_CHECK(live == 1);
    }
    BOOST_CHECK(live == 0);
}

BOOST_AUTO_TEST_SUITE_END()
