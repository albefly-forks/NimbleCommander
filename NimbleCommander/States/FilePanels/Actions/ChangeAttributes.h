#pragma once

#include "DefaultAction.h"

namespace panel::actions {

struct ChangeAttributes : PanelAction
{
    bool Predicate( PanelController *_target ) const override;
    void Perform( PanelController *_target, id _sender ) const override;
};

};