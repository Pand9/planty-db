#include <bits/stdc++.h>
#include "basic.h"

using index_t = i64;
using value_t = i64;
using vstr = std::vector<std::string>;
using indices_t = std::vector<index_t>;
using vi64 = std::vector<i64>;
using cname = std::string;
using cnames = std::vector<std::string>;
// note: some unique "column id" would be very useful, because we don't want Columns to have references to Table,
// and still we want to differentiate columns with the same name
// for now, we use cname for id's purposes


#define lprintln(...) lprintln("log:", __VA_ARGS__)

// {{{ exceptions and checks definitions
#define exception_check(ERROR_NAME, COND, MSG) do { if (!(COND)) throw ERROR_NAME(MSG); } while(0)
#define add_exception(ERROR_NAME, PARENT) struct ERROR_NAME : public PARENT { ERROR_NAME(std::string error) : PARENT(error) {} }

add_exception(data_error, std::runtime_error);
add_exception(query_format_error, data_error);
#define query_format_check(COND, MSG) exception_check(query_format_error, COND, MSG)
add_exception(table_error, data_error);
#define table_check(COND, MSG) exception_check(table_error, COND, MSG)
add_exception(query_semantics_error, data_error);
#define query_semantics_check(COND, MSG) exception_check(query_semantics_error, COND, MSG)
#undef add_exception
// }}}

// {{{ particular asserts
#define bound_assert(INDEX, CONTAINER) massert(INDEX >= 0 && INDEX < isize(CONTAINER), \
        "Index '" #INDEX "' = " + std::to_string(INDEX) + " out of bounds of container '" #CONTAINER \
        "' with size " + std::to_string(isize(CONTAINER)))
// }}}

class RowRange {
friend class RowNumbers;
friend class RowNumbersIterator;
public:
    RowRange() = default;
    RowRange(index_t l0, index_t r0) : l_(l0), r_(r0) {}
    bool operator==(const RowRange& other) const {
        return l_ == other.l_ && r_ == other.r_;
    }
    index_t l() const { return l_; }
    index_t& l() { return l_; }
    index_t r() const { return r_; }
    index_t& r() { return r_; }
    size_t size() const { return static_cast<size_t>(r_ - l_ + 1); }
    void normalize() { l_ = std::min(l_, r_ + 1); }
private:
    index_t l_, r_;
};

class IntColumn {
public:
    using ptr = std::unique_ptr<IntColumn>;
    static ptr make() { return std::make_unique<IntColumn>(); }
    void push_back(const value_t elem) { data_.push_back(elem); }
    cname& name() noexcept { return name_; }
    const cname& name() const noexcept { return name_; }
    const value_t& ref(index_t index) const noexcept { bound_assert(index, data_); return data_[index]; }
    index_t rows_count() const noexcept { return isize(data_); }
    // todo implement
    RowRange equal_range(value_t vall, value_t valr) const noexcept;
    RowRange equal_range(value_t val) const noexcept {
        auto r = std::equal_range(data_.begin(), data_.end(), val);
        return RowRange(r.first - data_.begin(), r.second - data_.begin() - 1);
    }
private:
    std::vector<value_t> data_;
    cname name_;
};

namespace pred { // {{{
template <class Pred> class Not;
template <class ...Ps> class And;
template <class ...Ps> class Or;
class IntEq;
class IntLess;
class IntRange;
class IntLessEq;
} // }}}

//class RowNumbers;
class RowNumbersIterator {
public:
    RowNumbersIterator() = default;
    RowNumbersIterator(bool is_range, indices_t::const_iterator indices_it, RowRange range)
        : indices_it_(indices_it), range_(range), is_range_(is_range)
    {}
    bool operator==(const RowNumbersIterator& other) const {
        return is_range_ ? (range_ == other.range_) : indices_it_ == other.indices_it_;
    }
    bool operator!=(const RowNumbersIterator& other) const {
        return !(*this == other);
    }
    RowNumbersIterator& operator++() {
        if (is_range_)
            ++range_.l();
        else
            ++indices_it_;
        return *this;
    }
    index_t operator*() const {
        if (is_range_)
            return range_.l();
        else
            return *indices_it_;
    }
    RowNumbersIterator make_end() const {
        return RowNumbersIterator(is_range_, indices_it_, RowRange(range_.r() + 1, range_.r()));
    }
private:
    indices_t::const_iterator indices_it_;
    RowRange range_;
    const bool is_range_;
};

class RowNumbers {
public:
    using iterator = RowNumbersIterator;
    RowNumbers(index_t len) : range_(0, len - 1), indices_set_used_(false) {}
    RowRange full_range() const { return range_; }

    iterator begin() const {
        return RowNumbersIterator(!indices_set_used_, indices_.cbegin(), range_);
    }
    iterator end() const {
        return RowNumbersIterator(!indices_set_used_, indices_.cend(), RowRange(range_.r() + 1, range_.r()));
    }
    void narrow(RowRange narrower) {
        massert(!indices_set_used_, "indices_set_ used during narrowing");
        range_.l() = std::max(range_.l(), narrower.l());
        range_.r() = std::min(range_.r(), narrower.r());
        range_.normalize();
    }
    void set_indices_set(indices_t&& indices) {
        indices_set_used_ = true;
        indices_ = indices;
    }
private:
    indices_t indices_;
    RowRange range_;
    bool indices_set_used_;
};

// metadata {{{
class Metadata {
public:
    Metadata(vstr&& columns0, i64 key_len0) : columns_(columns0), key_len_(key_len0) {
        table_check(key_len_ <= isize(columns_), "key_len " + std::to_string(key_len_) + " exceeds header length " + std::to_string(isize(columns_)));
    }
    const auto& columns() const { return columns_; }
    const auto& key_len() const { return key_len_; }
    auto _repr() const { return make_repr("Metadata", {"columns", "key_len"}, columns_, key_len_); }
private:
    vstr columns_;
    i64 key_len_;
};
// }}}
// input, output frame {{{
class InputFrame {
public:
    InputFrame(std::istream& is) : is_(is) {
    }
    Metadata get_metadata() {
        char sep = '\0';
        vstr header;
        i64 key_len = 0;
        bool metadata_found = false;
        while (sep != '\n' && !metadata_found) {
            std::string s;
            is_ >> s;
            table_check(!is_.eof(), "empty header");
            if (s.back() == ';') {
                s.pop_back();
                metadata_found = true;
            }
            header.push_back(s);
            is_.read(&sep, 1);
            table_check(!(sep != '\n' && sep != '\t' && sep != ' ' && sep != ';'),
                    "Bad separator: ascii code " + std::to_string(static_cast<int>(sep)));
        }
        if (metadata_found)
            is_ >> key_len;
        ++(*this);
        auto m = Metadata(std::move(header), std::move(key_len));
        dprintln(repr(m));
        return m;
    }
    vstr get_header() {
        char sep = '\0';
        vstr header;
        u64 key_len = 0;
        while (sep != '\n' && sep != ';') {
            std::string s;
            is_ >> s;
            table_check(!is_.eof(), "empty header");
            header.push_back(s);
            is_.read(&sep, 1);
            table_check(!(sep != '\n' && sep != '\t' && sep != ' ' && sep != ';'),
                    "Bad separator: ascii code " + std::to_string(static_cast<int>(sep)));
        }
        if (sep == ';')
            is_ >> key_len;
        dprintln("header", header);
        ++(*this);
        return header;
    }
    InputFrame& operator++() { is_ >> val_; return *this; }
    bool end() const { return is_.eof(); }
    value_t operator*() { return val_; }
private:
    value_t val_;
    std::istream& is_;
};
class OutputFrame {
public:
    OutputFrame(std::ostream& os) : os_(os) {
    }
    void add_header(const vstr& header) {
        table_check(!header.empty(), "header can't be empty");
        os_ << header[0];
        for (i64 i = 1; i < isize(header); i++)
            os_ << ' ' << header[i];
    }
    void new_row(const i64& val) { os_ << '\n' << val; }
    void add_to_row(const i64& val) {
        os_ << ' ' << val;
    }
    ~OutputFrame() {
        os_ << '\n';
        // add metadata etc, sure, why not
    }
private:
    std::ostream& os_;
};
// }}}
struct Query {
    vstr where_cols;
    vi64 where_vals;
    vstr select_cols;
    auto _repr() const { return make_repr("Query", {"where_cols", "where_vals", "select_cols"}, where_cols, where_vals, select_cols); }
};
class Table {
public:
    Table(Metadata metadata0, std::vector<IntColumn::ptr> columns0) : metadata_(std::move(metadata0)), columns_(std::move(columns0)) {}
public:
    // read/write {{{
    static Table read(InputFrame& frame) {
        auto metadata = frame.get_metadata();
        const auto& header = metadata.columns();
        std::vector<IntColumn::ptr> columns(metadata.columns().size());

        columns.resize(metadata.columns().size());
        for (i64 i = 0; i < isize(header); i++) {
            columns[i] = IntColumn::make();
            columns[i]->name() = header[i];
        }
        massert(!columns.empty(), "empty header");
        auto column_it = columns.begin();
        for (auto elem = *frame; !frame.end(); elem = *(++frame)) {
            (*column_it)->push_back(elem);
            if (++column_it == columns.end()) column_it = columns.begin();
        }
        table_check(column_it == columns.begin(), "couldn't read the same number of values for each column");
        Table tbl(std::move(metadata), std::move(columns));
        dprintln("rows:", tbl.rows_count(), "columns:", tbl.columns_count());
        return tbl;
    }
    void write(const cnames& names, const RowNumbers& rows, OutputFrame& frame) {
        frame.add_header(names);
        const auto columns = resolve_columns_(names);
        const size_t columns_count = columns.size();
        // TODO add this check to "query" class
        massert(columns_count > 0, "can't select 0 columns");
        for (const auto row_num : rows) {
            frame.new_row(columns_[columns[0]]->ref(row_num));
            for (u64 i = 1; i < columns_count; i++)
                frame.add_to_row(columns_[columns[i]]->ref(row_num));
        }
    }
    void validate(const Query& q) const {
        for (const auto& col : q.where_cols)
            resolve_column_(col);
        for (const auto& col : q.select_cols)
            resolve_column_(col);
    }
    // }}}
    const IntColumn::ptr& column(const cname& name) const noexcept {
        return columns_[resolve_column_(name)];
    }
    const IntColumn::ptr& column(const index_t& column_id) const noexcept {
        massert(column_id < isize(columns_), "index out of bound: " + std::to_string(column_id));
        return columns_[column_id];
    }
    index_t column_id(const cname& name) const noexcept { return resolve_column_(name); }
    size_t rows_count() const { return columns_[0]->rows_count(); }
    size_t columns_count() const { return isize(columns_); }
    const Metadata& metadata() const noexcept { return metadata_; }
private:
    indices_t resolve_columns_(const cnames& names) const {
        indices_t columns;
        std::transform(names.begin(), names.end(), std::back_inserter(columns),
                [this](const auto& name){ return this->resolve_column_(name); });
        return columns;
    }
    index_t resolve_column_(const cname& name) const {
        for (i64 i = 0; i < isize(columns_); i++)
            if (columns_[i]->name() == name)
                return i;
        table_check(false, "unknown column name: " + name); 
    }
    Metadata metadata_;
    std::vector<IntColumn::ptr> columns_;
};

// table playground {{{
class TablePlayground {
public:
    TablePlayground(Table& table) : table_(table) {}
    void run(vstr& where_cols, vi64& where_vals, vstr& select_cols, OutputFrame& outp) {
        RowNumbers rows(table_.rows_count());
        // TODO add this check to "query" class
        massert(where_cols.size() == where_vals.size(), "different cols and vals len");
        std::vector<std::vector<value_t>> filters(table_.columns_count());
        for (i64 i = 0; i < isize(where_cols); i++) {
            const value_t value = where_vals[i];
            const auto column_id = table_.column_id(where_cols[i]);
            filters[column_id].push_back(value);
        }
        i64 fullscan_cid = 0;
        for (; fullscan_cid < table_.metadata().key_len(); fullscan_cid++) {
            if (filters[fullscan_cid].empty())
                break;
        }
        dprintln(filters);
        lprintln("range scan for first " + std::to_string(fullscan_cid) + " columns");
        for (i64 column_id = 0; column_id < fullscan_cid; column_id++)
            for (const auto value : filters[column_id])
                rows.narrow(table_.column(column_id)->equal_range(value));

        {
            for (i64 cid = fullscan_cid; cid < isize(filters); cid++)
                std::sort(filters[cid].begin(), filters[cid].end());
            std::vector<index_t> remaining;
            for (const auto value : rows) {
                bool remains = true;
                for (i64 column_id = fullscan_cid; column_id < isize(filters); column_id++) {
                    remains &= filters[column_id].empty() ||
                        (filters[column_id].size() == 1 && filters[column_id][0] == table_.column(column_id)->ref(value));
//                    remains &= filters[column_id].empty() || std::binary_search(filters[column_id].begin(), filters[column_id].end(), table_.column(column_id)->ref(value));
                }
                if (remains)
                    remaining.push_back(value);
            }
            rows.set_indices_set(std::move(remaining));
        }
        table_.write(select_cols, rows, outp);
    }
private:
    Table& table_;
};
// }}}
// parse {{{
Query parse(const std::string line) {
    Query q;
    std::stringstream ss(line);
    std::string token;
    query_format_check(!ss.eof(), "empty line");
    ss >> token;
    query_format_check(token == "select", "no select at the beginning");
    dprint("<select>");
    {
        bool no_comma = true;
        while (no_comma && !ss.eof()) {
            ss >> token;
            if (token.back() != ',')
                no_comma = false;
            else
                token.pop_back();
            dprint(" [" + token + "]");
            q.select_cols.push_back(token);
        }
        query_format_check(!q.select_cols.empty(), "select list empty");
        query_format_check(!no_comma, "no comma after select list");
    }
    if (ss.eof()) {
        dprintln(';');
        return q;
    }
    ss >> token;
    query_format_check(token == "where", "something else than 'where' after select list: " + token);
    dprint(" <where>");
    {
        bool no_comma = true;
        while (no_comma && !ss.eof()) {
            ss >> token;
            if (token.back() != ',')
                no_comma = false;
            else
                token.pop_back();
            const auto sep = token.find_first_of("=");
            const auto col = token.substr(0, sep);
            const auto val = token.substr(sep + 1, isize(token) - sep - 1);
            dprint(" (" + col + "<=>" + val + ")");
            q.where_cols.push_back(col);
            q.where_vals.push_back(stoll(val));
        }
        query_format_check(!q.where_cols.empty(), "where list empty");
        query_format_check(!no_comma, "no comma after where list");
    }
    if (ss.eof()) {
        dprintln(";");
        return q;
    }
    ss >> token;
    query_format_check(false, "there's something after 'where': " + token);
}
// parse }}}
// main loop {{{
struct CmdArgs {
    std::string filename;
};
void main_loop(const CmdArgs& args) {
    std::ifstream ifs(args.filename);
    table_check(!ifs.fail(), "couldn't open database file " + args.filename);
    InputFrame file(ifs);
    auto tbl = Table::read(file);
    TablePlayground t(tbl);
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            println();
            auto q = parse(line);
            tbl.validate(q);
            OutputFrame outp(std::cout);
            dprintln(repr(q));
            t.run(q.where_cols, q.where_vals, q.select_cols, outp);
            dprintln();
        } catch (const data_error& e) {
            dprintln();
            println("query error:", e.what());
        }
    }
}
// }}}
// cmdline {{{
void quit(std::string msg) {
    lprintln(msg, "- exiting");
    exit(13);
}
CmdArgs validate(int argc, char** argv) {
    if (argc != 2)
        quit("one argument required: path to the csv file, tab-separated, with \"-escape character");
    return {std::string(argv[1])};
}
int main(int argc, char** argv) {
    main_loop(validate(argc, argv));
}
// }}}
