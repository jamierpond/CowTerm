#pragma once

#include "BoardDocument.h"

#include <emberstore/Emberstore.h>

namespace term
{
// ~/Library/Application Support/tamber/cowterm-board, beside CowTerm's session store.
eacp::FilePath getDefaultBoardStoreDirectory();

emberstore::Database openBoardDatabase(const eacp::FilePath& directory);
emberstore::Document<BoardDocument> openBoardDocument(const emberstore::Database& database);
} // namespace term
