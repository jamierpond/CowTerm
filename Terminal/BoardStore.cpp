#include "BoardStore.h"

namespace term
{
namespace
{
constexpr auto storeCompanyName = std::string_view {"tamber"};
constexpr auto storeAppName = std::string_view {"cowterm-board"};
constexpr auto boardDocumentName = std::string_view {"board"};
} // namespace

eacp::FilePath getDefaultBoardStoreDirectory()
{
    return eacp::FilePath::appDataDirectory() / storeCompanyName / storeAppName;
}

emberstore::Database openBoardDatabase(const eacp::FilePath& directory)
{
    return emberstore::Database {directory, emberstore::Durability::Atomic};
}

emberstore::Document<BoardDocument> openBoardDocument(const emberstore::Database& database)
{
    return database.document<BoardDocument>(boardDocumentName);
}
} // namespace term
