#include "VFSConfiguration.h"


struct VFSConfigurationDummyModel
{
    inline const char *Tag() const { return ""; }
    inline const char *Junction() const { return ""; }
    inline bool operator==(const VFSConfigurationDummyModel&) const { return false; }
};

VFSConfiguration::VFSConfiguration():
    m_Object( make_shared<Model<VFSConfigurationDummyModel>>( VFSConfigurationDummyModel() ) )
{
}

const char *VFSConfiguration::Tag() const
{
    return m_Object->Tag();
}

const char *VFSConfiguration::Junction() const
{
    return m_Object->Junction();
}

bool VFSConfiguration::Equal( const VFSConfiguration &_rhs ) const
{
    // logic:
    // 1) check if pointer hold the same object, if so, configs are equal
    // 2) check if types of hold object are the same, is not - configs can't be equal
    // 3) pass control into real configurations themselves, so they can check if they are equal
    if( m_Object == _rhs.m_Object )
        return true;
    if( m_Object->TypeID() != _rhs.m_Object->TypeID() )
        return false;
    return m_Object->Equal(*_rhs.m_Object);
}
