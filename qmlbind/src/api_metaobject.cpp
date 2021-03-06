#include "qmlbind/metaobject.h"
#include "metaobject.h"
#include "util.h"

using namespace QmlBind;

extern "C" {

qmlbind_metaobject qmlbind_metaobject_new(qmlbind_exporter exporter)
{
    return newSharedPointer(new MetaObject(*exporter));
}

void qmlbind_metaobject_release(qmlbind_metaobject metaobject)
{
    delete metaobject;
}

}
