#include "test_helper.h"
#include <catch.hpp>
#include <qmlbind.h>
#include <QMetaObject>
#include <QMetaMethod>
#include <QByteArray>
#include <QDebug>
#include <string.h>
#include <functional>

class Test
{
public:

    Test(std::function<void ()> onDestroy) :
        m_value(1),
        m_onDestroy(onDestroy)
    {
    }

    void incrementBy(double diff)
    {
        m_value += diff;
    }

    double value()
    {
        return m_value;
    }

    void setValue(double val)
    {
        m_value = val;
    }

private:
    double m_value;
    std::function<void ()> m_onDestroy;
};

namespace Handlers {

    void *newObject(void *klass)
    {
        std::string name = static_cast<char *>(klass);
        REQUIRE(name == "class:Test");
        return new Test([] {});
    }

    qmlbind_value *invokeMethod(void *obj, void *method, int argc, qmlbind_value **argv)
    {
        std::string name = static_cast<char *>(method);
        REQUIRE(name == "method:incrementBy");
        REQUIRE(argc == 1);

        auto test = static_cast<Test *>(obj);
        test->incrementBy(qmlbind_value_get_number(argv[0]));

        return qmlbind_value_new_number(test->value());
    }

    qmlbind_value *invokeGetter(void *obj, void *getter)
    {
        std::string name = static_cast<char *>(getter);
        REQUIRE(name == "getter:value");

        auto test = static_cast<Test *>(obj);
        return qmlbind_value_new_number(test->value());
    }

    void invokeSetter(void *obj, void *setter, qmlbind_value *value)
    {
        std::string name = static_cast<char *>(setter);
        REQUIRE(name == "setter:value");

        auto test = static_cast<Test *>(obj);
        test->setValue(qmlbind_value_get_number(value));
    }

    void deleteObject(void *obj)
    {
        delete static_cast<Test *>(obj);
    }
}

TEST_CASE("metaobject_exporter")
{
    auto engine = qmlbind_engine_new();

    qmlbind_interface_handlers handlers;

    handlers.new_object = &Handlers::newObject;
    handlers.call_method = &Handlers::invokeMethod;
    handlers.set_property = &Handlers::invokeSetter;
    handlers.get_property = &Handlers::invokeGetter;
    handlers.delete_object = &Handlers::deleteObject;

    auto interface = qmlbind_interface_new((void *)"class:Test", "Test", handlers);

    auto notifierIndex = qmlbind_interface_add_signal(interface, "valueChanged", 1);
    auto methodIndex = qmlbind_interface_add_method(interface, (void *)"method:incrementBy", "incrementBy", 1);
    auto propertyIndex = qmlbind_interface_add_property(interface, (void *)"getter:value", (void *)"setter:value", "value", notifierIndex);

    auto metaobject = qmlbind_metaobject_new(interface);

    SECTION("generated metaobject")
    {
        auto metaobj = reinterpret_cast<QMetaObject *>(metaobject);

        auto methodCount = metaobj->methodCount() - metaobj->methodOffset();
        auto propertyCount = metaobj->propertyCount() - metaobj->propertyOffset();

        REQUIRE(QByteArray(metaobj->className()) == "Test");
        REQUIRE(methodCount == 2);
        REQUIRE(propertyCount == 1);

        SECTION("method info")
        {
            auto method = metaobj->method(methodIndex + metaobj->methodOffset());
            REQUIRE(method.name() == "incrementBy");
            REQUIRE(method.methodType() == QMetaMethod::Method);
        }

        SECTION("notifier signal info")
        {
            auto method = metaobj->method(notifierIndex + metaobj->methodOffset());
            REQUIRE(method.name() == "valueChanged");
            REQUIRE(method.methodType() == QMetaMethod::Signal);
        }

        SECTION("property info")
        {
            auto prop = metaobj->property(propertyIndex + metaobj->propertyOffset());
            REQUIRE(QByteArray(prop.name()) == "value");
        }
    }

    SECTION("created wrapper")
    {
        bool destroyed = false;
        Test test([&] {
            destroyed = true;
        });

        auto value = qmlbind_engine_new_wrapper(engine, metaobject, &test);

        REQUIRE(qmlbind_value_is_wrapper(value));

        test.setValue(123);

        SECTION("getter")
        {
            auto prop = qmlbind_value_get_property(value, "value");
            REQUIRE(qmlbind_value_get_number(prop) == 123);
            qmlbind_value_delete(prop);
        }

        SECTION("setter")
        {
            auto prop = qmlbind_value_new_number(234);
            qmlbind_value_set_property(value, "value", prop);
            REQUIRE(test.value() == 234);
            qmlbind_value_delete(prop);
        }

        SECTION("method")
        {
            auto offset = qmlbind_value_new_number(100);
            auto func = qmlbind_value_get_property(value, "incrementBy");
            auto result = qmlbind_value_call_with_instance(func, value, 1, &offset);

            REQUIRE(test.value() == 223);

            qmlbind_value_delete(offset);
            qmlbind_value_delete(func);
            qmlbind_value_delete(result);
        }

        qmlbind_value_delete(value);

        //REQUIRE(destroyed);
    }

    SECTION("register type")
    {
        auto data = R"QML(
            import QtQml 2.2
            import test 1.0
            Test {
            }
        )QML";

        qmlbind_register_type(metaobject, "test", 1, 0, "Test");

        auto component = qmlbind_component_new(engine);
        qmlbind_component_set_data(component, data, "");

        auto error = qmlbind_component_get_error_string(component);
        if (error) {
            qDebug() << qmlbind_string_get(error);
            qmlbind_string_delete(error);
        }

        auto obj = qmlbind_component_create(component);

        {
            auto value = qmlbind_value_get_property(obj, "value");

            REQUIRE(qmlbind_value_get_number(value) == 1);

            qmlbind_value_delete(value);
        }

        {
            auto offset = qmlbind_value_new_number(100);
            auto func = qmlbind_value_get_property(obj, "incrementBy");
            auto result = qmlbind_value_call_with_instance(func, obj, 1, &offset);

            auto test = static_cast<Test *>(qmlbind_value_get_handle(obj));
            REQUIRE(test != nullptr);
            REQUIRE(test->value() == 101);

            qmlbind_value_delete(offset);
            qmlbind_value_delete(func);
            qmlbind_value_delete(result);
        }

        qmlbind_value_delete(obj);

        qmlbind_component_delete(component);
    }

    qmlbind_metaobject_delete(metaobject);
    qmlbind_engine_delete(engine);
}
