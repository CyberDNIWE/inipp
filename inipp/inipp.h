/*
MIT License

Copyright (c) 2017-2020 Matthias C. M. Troffaes

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include <cstring>
#include <string>
#include <iostream>
#include <list>
#include <locale>
#include <map>
#include <algorithm>
#include <functional>
#include <cctype>
#include <sstream>

namespace inipp {

namespace detail {

// trim functions based on http://stackoverflow.com/a/217605

template <class CharT>
inline void ltrim(std::basic_string<CharT> & s, const std::locale & loc) {
	s.erase(s.begin(),
                std::find_if(s.begin(), s.end(),
                             [&loc](CharT ch) { return !std::isspace(ch, loc); }));
}

template <class CharT>
inline void rtrim(std::basic_string<CharT> & s, const std::locale & loc) {
	s.erase(std::find_if(s.rbegin(), s.rend(),
                             [&loc](CharT ch) { return !std::isspace(ch, loc); }).base(),
                s.end());
}

// string replacement function based on http://stackoverflow.com/a/3418285

template <class CharT>
inline bool replace(std::basic_string<CharT> & str, const std::basic_string<CharT> & from, const std::basic_string<CharT> & to) {
	auto changed = false;
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::basic_string<CharT>::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length();
		changed = true;
	}
	return changed;
}

} // namespace detail

template <typename CharT, typename T>
inline bool extract(const std::basic_string<CharT> & value, T & dst) {
	CharT c;
	std::basic_istringstream<CharT> is{ value };
	T result;
	if ((is >> std::boolalpha >> result) && !(is >> c)) {
		dst = result;
		return true;
	}
	else {
		return false;
	}
}

template <typename CharT>
inline bool extract(const std::basic_string<CharT> & value, std::basic_string<CharT> & dst) {
	dst = value;
	return true;
}

class CharTEBase
{
public:
	CharTEBase() = default;
	virtual ~CharTEBase() = default;

	virtual bool equals(const void* other) const = 0;
};

template<typename CHAR>
class TEChar : public CharTEBase
{
public:
	TEChar(const CHAR c) : m_char(c) {};
	virtual ~TEChar() = default;

	virtual bool equals(const void* other) const override
	{
		bool ret = (m_char && other);
		if(ret)
		{
			ret = (*m_char == *static_cast<CHAR>(other));
		}
		return ret;
	}

protected:
	const CHAR m_char;

private:
	TEChar() = default;
};

class CharTypeErased
{
public:	
	template<typename CHAR>
	CharTypeErased(const CHAR ch) : m_ch(new TEChar<CHAR>(ch))
	{}

	virtual ~CharTypeErased()
	{
		delete m_ch;
	}

	template <typename CHAR>
	bool equals(const CHAR& other) const
	{
		return m_ch->equals(&other);
	}

protected:
	CharTEBase* m_ch = nullptr;

private:
	CharTypeErased() = default;
};

template <typename CharT>
class CommentCheckable
{
public:
	CommentCheckable() = default;
	virtual ~CommentCheckable() = default;

	virtual bool is_comment(const CharTypeErased& che) const = 0;
};

template<class CharT>
class Ini : protected CommentCheckable<CharT>
{
public:
	typedef std::basic_string<CharT> String;
	typedef std::map<String, String> Section;
	typedef std::map<String, Section> Sections;

	Sections sections;
	std::list<String> errors;

	static const CharT char_section_start  = static_cast<CharT>('[');
	static const CharT char_section_end    = static_cast<CharT>(']');
	static const CharT char_assign         = static_cast<CharT>('=');
	static const CharT char_comment		   = static_cast<CharT>(';');
	static const CharT char_interpol       = static_cast<CharT>('$');
	static const CharT char_interpol_start = static_cast<CharT>('{');
	static const CharT char_interpol_sep   = static_cast<CharT>(':');
	static const CharT char_interpol_end   = static_cast<CharT>('}');

	static const int max_interpolation_depth = 10;

	Ini() = default;
	virtual ~Ini() = default;

	void generate(std::basic_ostream<CharT> & os) const {
		for (auto const & sec : sections) {
			os << char_section_start << sec.first << char_section_end << std::endl;
			for (auto const & val : sec.second) {
				os << val.first << char_assign << val.second << std::endl;
			}
			os << std::endl;
		}
	}

	void parse(std::basic_istream<CharT> & is) {
		String line;
		String section;
		const std::locale loc{"C"};
		while (std::getline(is, line)) {
			detail::ltrim(line, loc);
			detail::rtrim(line, loc);
			const auto length = line.length();
			if (length > 0) {
				const auto pos = line.find_first_of(char_assign);
				const auto & front = line.front();
				if (is_comment(CharTypeErased(&front))) {
					continue;
				}
				else if (front == char_section_start) {
					if (line.back() == char_section_end)
						section = line.substr(1, length - 2);
					else
						errors.push_back(line);
				}
				else if (pos != 0 && pos != String::npos) {
					String variable(line.substr(0, pos));
					String value(line.substr(pos + 1, length));
					detail::rtrim(variable, loc);
					detail::ltrim(value, loc);
					auto & sec = sections[section];
					if (sec.find(variable) == sec.end())
						sec.insert(std::make_pair(variable, value));
					else
						errors.push_back(line);
				}
				else {
					errors.push_back(line);
				}
			}
		}
	}

	void interpolate() {
		int global_iteration = 0;
		auto changed = false;
		// replace each "${variable}" by "${section:variable}"
		for (auto & sec : sections)
			replace_symbols(local_symbols(sec.first, sec.second), sec.second);
		// replace each "${section:variable}" by its value
		do {
			changed = false;
			const auto syms = global_symbols();
			for (auto & sec : sections)
				changed |= replace_symbols(syms, sec.second);
		} while (changed && (max_interpolation_depth > global_iteration++));
	}

	void default_section(const Section & sec) {
		for (auto & sec2 : sections)
			for (const auto & val : sec)
				sec2.second.insert(val);
	}

	void clear() {
		sections.clear();
		errors.clear();
	}

protected:
	virtual bool is_comment(const CharTypeErased& che) const override
	{
		return che.equals(char_comment);
	}

private:
	typedef std::list<std::pair<String, String> > Symbols;

	auto local_symbol(const String & name) const {
		return char_interpol + (char_interpol_start + name + char_interpol_end);
	}

	auto global_symbol(const String & sec_name, const String & name) const {
		return local_symbol(sec_name + char_interpol_sep + name);
	}

	auto local_symbols(const String & sec_name, const Section & sec) const {
		Symbols result;
		for (const auto & val : sec)
			result.push_back(std::make_pair(local_symbol(val.first), global_symbol(sec_name, val.first)));
		return result;
	}

	auto global_symbols() const {
		Symbols result;
		for (const auto & sec : sections)
			for (const auto & val : sec.second)
				result.push_back(
					std::make_pair(global_symbol(sec.first, val.first), val.second));
		return result;
	}

	bool replace_symbols(const Symbols & syms, Section & sec) const {
		auto changed = false;
		for (auto & sym : syms)
			for (auto & val : sec)
				changed |= detail::replace(val.second, sym.first, sym.second);
		return changed;
	}
};

} // namespace inipp

 /* Usage for extending inipp::Ini<CharT>:

 #include <array>
// VisualbasicIni.h
class VisualBasicIni : public inipp::Ini<char>
{
public:
	VisualBasicIni() = default;
	virtual ~VisualBasicIni() = default;

protected:
	//This one for demo only, could be whatever logic user wants
	static const std::array<const char, 2> m_comment_chars;

	virtual bool is_comment(const inipp::CharTypeErased& chg) const override;
};

//VisualBasicIni.cpp
const std::array<const char, 2> VisualBasicIni::m_comment_chars = { inipp::Ini<char>::char_comment, '\'' };
bool VisualBasicIni::is_comment(const inipp::CharTypeErased& chg) const
{
	bool ret = false;
	for(const auto& c : m_comment_chars)
	{		
		if(chg.equals(c))
		{
			ret = true;
		}
	}
	return ret;
}

*/