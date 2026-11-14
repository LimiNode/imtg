#ifndef IMTG_ACTIONS_ACTION_COMMAND_HPP
#define IMTG_ACTIONS_ACTION_COMMAND_HPP

/// \file imtg/actions/ActionCommand.hpp
/// \brief Standalone public component; no preceding umbrella include is required.

#include <string>

namespace imtg {

/// \brief Backend-neutral command emitted by an action handler.
struct ActionCommand {
    std::string type; ///< Application-defined command route.
    std::string payload; ///< Application-defined command bytes.
};

} // namespace imtg

#endif // IMTG_ACTIONS_ACTION_COMMAND_HPP
