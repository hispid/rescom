#include "StringHelpers.hpp"
#include <algorithm>


void replaceAll(std::string& str, char toReplace, char replacement)
{
    for (auto pos = str.find(toReplace); pos != std::string::npos; pos = str.find(toReplace, pos + 1))
    {
        str[pos] = replacement;
    }
}

std::string toUpper(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return static_cast<char>(std::toupper(c)); });

    return str;
}

std::string toLower(std::string str)
{
  std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

  return str;
}

std::string_view trim(std::string_view view)
{
    static constexpr char const* const Spaces = " \t\v\f\r\n";
    view.remove_prefix(std::min(view.find_first_not_of(Spaces), view.size()));
    view.remove_suffix(view.size() - std::min(view.find_last_not_of(Spaces) + 1, view.size()));

    return view;
}

std::string_view removeComment(std::string_view view, char const* oneLineCommentStart)
{
    auto commentStartPosition = view.find(oneLineCommentStart);

    if (commentStartPosition != std::string_view::npos)
        view.remove_suffix(view.size() - commentStartPosition);

    return view;
}

