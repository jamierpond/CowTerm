#pragma once

#include <cstddef>
#include <vector>

namespace term
{
// The numbers of one control sequence: groups split by ';', each holding the
// ':'-separated sub-parameters (38:2::r:g:b keeps its parts in one group).
class CsiParameters
{
public:
    using Group = std::vector<int>;

    void clear();
    void appendDigit(int digit);
    void startGroup();
    void startSubParameter();

    std::size_t size() const { return groups.size(); }
    const Group& group(std::size_t index) const { return groups[index]; }

    // The group's first number, or fallback when it is missing or empty.
    int value(std::size_t index, int fallback) const;

    // As value(), but a missing, empty or zero count means one.
    int count(std::size_t index) const;

    // True once the sequence held more numbers than any real one uses
    // (Ghostty's limit); the caller drops such a sequence.
    bool hasTooManyNumbers() const { return tooManyNumbers; }

private:
    Group& lastGroupWithAValue();
    bool canAddNumber();

    std::vector<Group> groups;
    std::size_t numberCount = 1;
    bool tooManyNumbers = false;
};
} // namespace term
