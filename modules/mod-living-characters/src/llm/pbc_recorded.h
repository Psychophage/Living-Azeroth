// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Isolated game tests. Recorded responses never open a network connection.
#ifndef PBC_RECORDED_H
#define PBC_RECORDED_H

#include "pbc_model.h"

namespace PBC
{
HttpTransport RecordedTransport(std::string const& fixtureJson);
}

#endif
