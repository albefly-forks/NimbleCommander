#pragma once

#include "DefaultAction.h"

namespace nc::panel::actions {

// external dependency - ActivationManager

struct ExecuteInTerminal : PanelAction
{
    bool Predicate( PanelController *_target ) const override;
    void Perform( PanelController *_target, id _sender ) const override;
};

}