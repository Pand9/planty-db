#include <bits/stdc++.h>
#include "basic.h"
#include "metaclass.h"
#include "defs.h"
#include "measure.h"
#include "ranges.h"

using namespace std::string_literals;

// {{{ exceptions and checks definitions
#define exception_check(ERROR_NAME, COND, MSG) do { if (!(COND)) throw ERROR_NAME(MSG); } while(0)

#define add_exception(ERROR_NAME, PARENT) struct ERROR_NAME : public PARENT { ERROR_NAME(string const& error) : PARENT(error) {} }

add_exception(data_error, std::runtime_error);

add_exception(query_format_error, data_error);
#define query_format_check(COND, MSG) exception_check(query_format_error, COND, MSG)

add_exception(table_error, data_error);
#define table_check(COND, MSG) exception_check(table_error, COND, MSG)

add_exception(query_semantics_error, data_error);
#define query_semantics_check(COND, MSG) exception_check(query_semantics_error, COND, MSG)

#undef add_exception
// }}}
class RowRange { // {{{
public:
    using iterator = IntIterator<index_t>;
    RowRange() = default;
    RowRange(RowRange const&) = default;
    RowRange & operator=(RowRange const&) = default;
    RowRange & operator=(RowRange &&) = default;
    RowRange(RowRange &&) = default;
    RowRange(index_t l0, index_t r0) : l_(l0), r_(r0) {}
    bool operator==(const RowRange& other) const { return l_ == other.l_ && r_ == other.r_; }
    bool operator<(const RowRange& other) const { return l_ != other.l_ ? l_ < other.l_ : r_ < other.r_; }
    index_t l() const { return l_; }
    index_t& l() { return l_; }
    index_t r() const { return r_; }
    index_t& r() { return r_; }
    index_t len() const { return r_ - l_ + 1; }
    void normalize() { l_ = std::min(l_, r_ + 1); }
    auto begin() const { return iterator(l_); }
    auto end() const { return iterator(r_ + 1); }
    std::string _str() const { return "<" + str(l_) + ".." + str(r_) + ">"; }
    std::string _repr() const { return make_repr("RowRange", {"l", "r"}, l_, r_); }
private:
    index_t l_, r_;
}; // }}}
// ValueInterval {{{
i64 string_to_int(std::string_view strv) {
    std::size_t first_non_converted = 0;
    i64 const res = std::stoll(std::string(strv), &first_non_converted);
    query_format_check(first_non_converted == strv.size(),
            "Error during converting to integer: " + string(strv));
    return res;
}
constexpr char const* interval_separator = "..";
class ValueInterval {
public:
    ValueInterval(std::string_view strv) {
        auto const sep_pos = strv.find(interval_separator);
        l_ = (r_ = 0);
        if (sep_pos == std::string_view::npos) {
            auto const val = string_to_int(strv);
            l_open_ = false;
            r_open_ = false;
            l_infinity_ = false;
            r_infinity_ = false;
            l_ = val;
            r_ = val;
            return;
        }
        // e.g. [1..2), (1..4], (..3), (..), [2..)
        query_format_check(sep_pos > 0 && sep_pos + 2 < strv.size(), "bad '..' position: " + string(strv));
        l_open_ = strv.front() == '(';
        r_open_ = strv.back() == ')';
        query_format_check(l_open_ || strv.front() == '[', "bad range open: " + string(strv));
        query_format_check(r_open_ || strv.back() == ']', "bad range close: " + string(strv));
        l_infinity_ = (sep_pos == 1);
        r_infinity_ = (sep_pos + 2 == strv.size() - 1);
        if (!l_infinity_)
            l_ = string_to_int(strv.substr(1, sep_pos - 1));
        if (!r_infinity_)
            r_ = string_to_int(strv.substr(sep_pos + 2, strv.size() - 1 - (sep_pos + 2)));
    }
    ValueInterval(value_t l0, value_t r0, bool l_open0 = false, bool r_open0 = false,
        bool l_infinity0 = false, bool r_infinity0 = false) noexcept : l_(l0), r_(r0), l_open_(l_open0),
            r_open_(r_open0), l_infinity_(l_infinity0), r_infinity_(r_infinity0) {}
    value_t const& l() const noexcept { return l_; }
    value_t const& r() const noexcept { return r_; }
    bool l_open() const noexcept { return l_open_; }
    bool r_open() const noexcept { return r_open_; }
    bool l_infinity() const noexcept { return l_infinity_; }
    bool r_infinity() const noexcept { return r_infinity_; }
    bool is_single_value() const noexcept {
        return !l_infinity_ && !r_infinity_ && !l_open_ && !r_open_ && l_ == r_;
    }
    bool contains(value_t const& v) const noexcept {
        bool const l_contains = l_infinity_ || (l_open_ ? (l_ < v) : (l_ <= v));
        bool const r_contains = r_infinity_ || (r_open_ ? (r_ > v) : (r_ >= v));
        return l_contains && r_contains;
    }
    std::string _repr() const {
        auto bracket1 = (l_open() ? '(' : '[');
        auto bracket2 = (r_open() ? ')' : ']');
        auto value1 = (l_infinity() ? std::string("") : repr(l_));
        auto value2 = (r_infinity() ? std::string("") : repr(r_));
        return bracket1 + value1 + ".." + value2 + bracket2;
    }
    std::string _str() const { return _repr(); }
private:
    value_t l_, r_;
    bool l_open_, r_open_, l_infinity_, r_infinity_;
}; // }}}
class IntColumn { // {{{
public:
    using ref = std::reference_wrapper<const IntColumn>;
    using ptr = std::unique_ptr<IntColumn>;

    static ptr make() { return std::make_unique<IntColumn>(); }

    void push_back(const value_t elem) { data_.push_back(elem); }
    cname& name() noexcept { return name_; }
    const cname& name() const noexcept { return name_; }
    const value_t& at(index_t index) const noexcept { bound_assert(index, data_); return data_[index]; }
    index_t rows_count() const noexcept { return isize(data_); }
    RowRange equal_range(const RowRange& rng, value_t val) const noexcept {
        auto const r = std::equal_range(data_.begin() + rng.l(), data_.begin() + rng.r() + 1, val);
        return RowRange(r.first - data_.begin(), r.second - data_.begin() - 1);
    }
    RowRange equal_range(RowRange const& rng, ValueInterval const& val) const noexcept {
        auto l = rng.l();
        if (!val.l_infinity()) {
            auto const l_rng = equal_range(rng, val.l());
            l = val.l_open() ? (l_rng.r() + 1) : l_rng.l();
        }
        auto r = rng.r();
        if (!val.r_infinity()) {
            auto const r_rng = equal_range(rng, val.r());
            r = val.r_open() ? (r_rng.l() - 1) : r_rng.r();
        }
        return RowRange(l, r);
    }
    std::string _repr() const { return make_repr("IntColumn", {"name", "length"}, name_, isize(data_)); }
private:
    std::vector<value_t> data_;
    cname name_;
}; // }}}
class RowNumbers; // {{{
class RowNumbersEraser {
public:
    RowNumbersEraser(RowNumbers & rows) : rows_(rows) { indices_.reserve(65536); }
    ~RowNumbersEraser() noexcept;
    // note: opportunity for optimization
    void keep(index_t idx) { massert2(indices_.empty() || indices_.back() < idx); indices_.push_back(idx); }
private:
    RowNumbers & rows_;
    indices_t indices_;

};
class RowNumbers {
    friend class RowNumbersEraser;
public:
    RowNumbers(RowRange const& range) noexcept : range_(range) {}
    RowRange as_range() const noexcept {
        massert2(!indices_set_used_);
        return range_;
    }
    void narrow(RowRange narrower) noexcept {
        massert(!indices_set_used_, "indices_set_ used during narrowing");
        range_.l() = std::max(range_.l(), narrower.l());
        range_.r() = std::min(range_.r(), narrower.r());
        range_.normalize();
    }
    template <typename F>
    void foreach(const F& f) const {
        if (indices_set_used_)
            for (const auto& row_id : indices_)
                f(row_id);
        else
            for (i64 row_id = range_.l(); row_id <= range_.r(); row_id++)
                f(row_id);
    }
    template <typename F>
    static void foreach(std::vector<RowNumbers> const& rows, F const& f) {
        for (auto const& r : rows)
            r.foreach(f);
    }
    std::string _repr() const {
        return make_repr("RowNumbers", {"indices"}, str(*this));
    }
    std::string _str() const {
        return indices_set_used_ ? "{" + str(isize(indices_)) + " rows}" : str(range_);
    }
private:
    RowRange range_;
    indices_t indices_ = {};
    bool indices_set_used_ = false;
};
RowNumbersEraser::~RowNumbersEraser() noexcept {
    massert2(!rows_.indices_set_used_);
    if (isize(indices_) == rows_.range_.len())
        return;
    rows_.indices_set_used_ = true;
    rows_.indices_ = std::move(indices_);
}
// }}}
// metadata {{{
class Metadata {
public:
    Metadata(vstr&& columns0, i64 key_len0) : columns_(columns0), key_len_(key_len0) {
        massert2(!columns_.empty());
        table_check(key_len_ <= columns_count(),
                "key_len " + str(key_len_) + " exceeds header length " + str(columns_count()));
    }
    i64 columns_count() const { return isize(columns_); }
    index_t column_id(const cname& name) const { return resolve_column_(name); }
    indices_t column_ids(const cnames& names) const {
        indices_t res;
        for (auto const& name : names)
            res.push_back(resolve_column_(name));
        return res;
    }
    auto const& column_name(i64 const& id) const { bound_assert(id, columns_); return columns_[id]; }
    auto const& column_names() const { return columns_; }
    IntRange columns() const { return IntRange(0, columns_count()); }
    auto const& key_len() const { return key_len_; }
    IntRange key_columns() const { return IntRange(0, key_len()); }
    std::string _repr() const { return make_repr("Metadata", {"columns", "key_len"}, columns_, key_len_); }
private:
    index_t resolve_column_(cname const& name) const {
        for (auto const i : columns())
            if (columns_[i] == name)
                return i;
        throw table_error("unknown column name: " + name);
    }
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
// table {{{
class ColumnHandle;
class Table {
public:
    Table(Metadata metadata0, std::vector<IntColumn::ptr> columns0)
            : md_(std::move(metadata0)), columns_(std::move(columns0)) {
        massert2(md_.columns_count() == isize(columns_));
    }
public:
    static Table read(InputFrame& frame);
    void write(const vector<ColumnHandle>& columns, const std::vector<RowNumbers>& rows, OutputFrame& frame);
    void write(const cnames& names, const vector<RowNumbers>& rows, OutputFrame& frame);

    i64 rows_count() const { return columns_[0]->rows_count(); }
    RowRange row_range() const { return RowRange(0, rows_count() - 1); }
    const IntColumn::ptr& column(const cname& name) const {
        return columns_[md_.column_id(name)];
    }
    const IntColumn::ptr& column(const index_t& column_id) const {
        bound_assert(column_id, columns_);
        return columns_[column_id];
    }
    const Metadata& metadata() const noexcept { return md_; }

    index_t column_id(const cname& name) const { return md_.column_id(name); }
    IntRange key_columns() const { return md_.key_columns(); }
    i64 columns_count() const { return md_.columns_count(); }
    IntRange columns() const { return md_.columns(); }
private:
    Metadata md_;
    std::vector<IntColumn::ptr> columns_;
};
// }}}
// column handle {{{
class ColumnHandle {
public:
    ColumnHandle(Table const& tbl, cname name) :
        col_id_(tbl.column_id(name)), col_(*tbl.column(col_id_)) {}
    ColumnHandle(Table const& tbl, i64 column_id) :
        col_id_(column_id), col_(*tbl.column(col_id_)) {}
    const IntColumn& ref() const { return col_; }
    index_t id() const { return col_id_; }
    std::string _repr() const { return make_repr("ColumnHandle", {"column"}, col_.get()); }
private:
    index_t col_id_;
    const IntColumn::ref col_;
};
// }}}
// read/write {{{
void Table::write(const vector<ColumnHandle>& columns, const vector<RowNumbers>& rows, OutputFrame& frame) {
    // todo un-lazy it
    vstr names;
    for (const auto& col : columns)
        names.push_back(col.ref().name());
    write(names, rows, frame);
}
Table Table::read(InputFrame& frame) {
    auto md = frame.get_metadata();
    std::vector<IntColumn::ptr> columns(md.columns_count());
    for (auto const i : md.columns()) {
        columns[i] = IntColumn::make();
        columns[i]->name() = md.column_name(i);
    }
    auto column_it = columns.begin();
    for (auto elem = *frame; !frame.end(); elem = *(++frame)) {
        (*column_it)->push_back(elem);
        if (++column_it == columns.end()) column_it = columns.begin();
    }
    table_check(column_it == columns.begin(), "couldn't read the same number of values for each column");
    Table tbl(std::move(md), std::move(columns));
    dprintln("rows:", tbl.rows_count(), "columns:", tbl.columns_count());
    return tbl;
}
void Table::write(const cnames& names, const std::vector<RowNumbers>& rows, OutputFrame& frame) {
    frame.add_header(names);
    auto const columns = md_.column_ids(names);
    auto const columns_count = isize(columns);
    massert(columns_count > 0, "can't select 0 columns");
    RowNumbers::foreach(rows,
            [&frame, &columns_ = columns_, columns_count, &columns]
            (i64 row_num) {
        frame.new_row(columns_[columns[0]]->at(row_num));
        for (auto const i : IntRange(1, columns_count))
            frame.add_to_row(columns_[columns[i]]->at(row_num));
    });
}
// }}}
// expression bindings {{{
class ConstBinding {
public:
    ConstBinding(value_t value0) : value_(value0) {}
add_field_with_lvalue_ref_accessor(value_t, value)
};
class ColumnBinding {
public:
    ColumnBinding(const IntColumn& col, const RowRange& rng) : col_(col), rng_(rng) {}
    const IntColumn& column() const { return col_; }
    const RowRange& row_range() const { return rng_; }
    template <typename T>
    auto equal_range(const T& value) const {
        return column().equal_range(row_range(), value);
    }
    std::string _repr() const { return make_repr("ColumnBinding", {"column", "row_range"}, col_, rng_); }
private:
    const IntColumn& col_;
    const RowRange& rng_;
};
// }}}
class PredOp { // {{{
public:
    enum Op : char {op_less = '<', op_equal = '=', op_more = '>'};
    PredOp(const std::string& op) {
        if (op == "<") op_ = op_less;
        else if (op == "=") op_ = op_equal;
        else if (op == ">") op_ = op_more;
        else unreachable_assert("unknown operator string in PredOp constructor: " + op);
    }
    bool is_single_elem() const { return op_ == op_equal; }
    std::string _str() const { return _repr(); }
    std::string _repr() const { return std::string(1, static_cast<char>(op_)); }
private:
    Op op_;
}; // }}}
class ColumnPredicate { // {{{
public:
    ColumnPredicate(ColumnHandle h, vector<ValueInterval> intervals)
        : col_(h), intervals_(intervals) {}
    template <class AddResult>
    void filter(std::vector<RowRange> const& rows, AddResult add_result) const {
        for (RowRange const& range : rows)
            for (ValueInterval const& values : intervals_)
                add_result(col_.ref().equal_range(range, values), values);
    }
    bool match_row_id(const index_t& idx) const noexcept {
        auto const& value = col_.ref().at(idx);
//        auto const it = std::upper_bound(intervals_.begin(), intervals_.end(), value,
//                [](value_t const& l, ValueInterval const& r) {
//                    return !r.contains(l) && (r.r_infinity() || l < r.r());
//                });
//
//        return !intervals_.empty() && intervals_.front().contains(value);
        for (auto const& interval : intervals_)
            if (interval.contains(value))
                return true;
        return false;
    }
    std::string _repr() const { return make_repr("ColumnPredicate", {"column", "intervals"}, col_, intervals_); }
private:
    const ColumnHandle col_;
    vector<ValueInterval> intervals_;
}; // }}}
// query {{{
using columns_t = std::vector<ColumnHandle>;
// after info about key and column order was used
struct FullscanRequest {
    FullscanRequest(RowRange range, i64 fcol) : rows(range), last_range_col(fcol) {}
    FullscanRequest(FullscanRequest const&) = default;
    FullscanRequest(FullscanRequest &&) = default;
    FullscanRequest & operator=(FullscanRequest &&) = default;
    FullscanRequest & operator=(FullscanRequest const&) = default;
    bool operator<(const FullscanRequest& other) const { return rows < other.rows; }
    RowRange rows;
    i64 last_range_col;
    std::string _repr() const { return make_repr("FullscanRequest", {"rows", "last_range_col"}, rows, last_range_col); }
    std::string _str() const { return "(last_col=" + str(last_range_col) + ", rows=" + str(rows) + ")"; }
};
class AfterRangeScan {
public:
    AfterRangeScan(std::vector<FullscanRequest> fullscan_requests_0) noexcept
            : fullscan_requests_(std::move(fullscan_requests_0)) {
        std::sort(fullscan_requests_.begin(), fullscan_requests_.end());
    }
    vector<FullscanRequest> const& fullscan_requests() const noexcept { return fullscan_requests_; }
    std::string _repr() const {
        return make_repr("AfterRangeScan", {"fullscan_requests"}, fullscan_requests_);
    }
    std::string _str() const {
        return str(fullscan_requests_);
    }
private:
    vector<FullscanRequest> fullscan_requests_;
};
class AfterFullscan {
public:
    AfterFullscan(vector<RowNumbers> rows_0) noexcept : rows_(std::move(rows_0)) {}
    vector<RowNumbers> const& rows() const noexcept { return rows_; }
    std::string _repr() const { return make_repr("AfterFullscan", {"rows"}, rows_); }
    std::string _str() const { return _repr(); }
private:
    vector<RowNumbers> rows_;
};
// }}}
// {{{ table predicate
class TablePredicate {
public:
    TablePredicate(Metadata const& md, vector<ColumnPredicate> && preds_0) : md_(md), preds_(preds_0) {}
    AfterRangeScan perform_range_scan(RowRange const& rows) const {
        vector<FullscanRequest> outp;
        vector<RowRange> rows_to_rangescan = {rows};
        vector<RowRange> rows_to_rangescan_rotate = {};
        for (const i64 c : md_.key_columns()) {
            if (rows_to_rangescan.empty())
                break;
            log_plan("Range scan for column:", str(c), "rows:", str(rows_to_rangescan));
            auto& pred = preds_[c];
            bool const is_last_range_column = (c + 1 == md_.key_len());
            auto result_handler = [&] (RowRange r, ValueInterval const& v) {
                    if (!is_last_range_column && v.is_single_value())
                        rows_to_rangescan_rotate.push_back(r);
                    else
                        outp.emplace_back(r, c);
                };
            pred.filter(rows_to_rangescan, result_handler);
            std::swap(rows_to_rangescan, rows_to_rangescan_rotate);
            rows_to_rangescan_rotate.clear();
        }
        return AfterRangeScan(std::move(outp));
    }
    RowNumbers perform_full_scan(RowRange const& rows, IntRange const& columns) const {
        RowNumbers row_numbers(rows);
        {
            RowNumbersEraser eraser(row_numbers);
            for (auto const i : row_numbers.as_range()) {
                bool can_stay = true;
                for (auto const c : columns)
                    can_stay &= preds_[c].match_row_id(i);
                if (can_stay)
                    eraser.keep(i);
            }
        }
        return row_numbers;
    }
    vector<RowNumbers> perform_full_scan(vector<FullscanRequest> const& requests) const {
        std::vector<RowNumbers> outp;
        for (auto const& request : requests)
            outp.push_back(perform_full_scan(request.rows,
                        IntRange(request.last_range_col + 1, md_.columns_count())));
        return outp;
    }
    std::string _repr() const {
        auto s = "TablePredicate(\n"s;
        for (auto const& pred : preds_)
            s += "    " + repr(pred) + "\n";
        return s;
    }
private:
    Metadata const& md_;
    vector<ColumnPredicate> preds_;
};
struct Query {
    TablePredicate where_pred;
    columns_t select_cols;
    std::string _repr() const { return make_repr("Query", {"where_preds", "select_cols"}, where_pred, select_cols); }
};
// }}}
class TablePlayground { // {{{
public:
    TablePlayground(Table & table) : table_(table) {}
    void run(Query const& q, OutputFrame & outp) {
        auto const after_range = q.where_pred.perform_range_scan(table_.row_range());
        log_plan("Range scan result:", str(after_range));
        auto const rows = q.where_pred.perform_full_scan(after_range.fullscan_requests());
        log_plan("Full scan result:", str(rows));
        table_.write(q.select_cols, rows, outp);
    }
    void validate() const {
        vi64 prev_value;
        for (auto const i : table_.row_range()) {
            vi64 current_value;
            for (auto const j : table_.key_columns())
                current_value.push_back(table_.column(j)->at(i));
            table_check(i == 0 || !vector_less(current_value, prev_value),
                "row " + std::to_string(i) + "'s key is lesser than previous row");
            prev_value = std::move(current_value);
        }
    }
private:
    Table& table_;
}; // }}}
class SingleRangePred { // {{{
public:
    template <class T1, class T2>
    SingleRangePred(T1 && op_0, T2 && value_0) : op_(op_0), value_(std::string_view(value_0)) {}
    PredOp const& op() const noexcept { return op_; }
    ValueInterval const& value() const noexcept { return value_; }
    std::string _repr() const {
        return make_repr("SingleRangePred", {"op", "value"}, op_, value_);
    }
    std::string _str() const { return _repr(); }
private:
    PredOp op_;
    ValueInterval value_;
};
class RangePredBuilder {
public:
    RangePredBuilder(Metadata const& md_0, i64 column_id_0) : md_(md_0), column_id_(column_id_0) {}
    template <class ...Ts>
    void add_pred(Ts && ...ts) { single_preds_.emplace_back(ts...); }
    ColumnPredicate build(Table const& tbl) {
        auto intervals = organize(single_preds_);
        if (intervals.empty())
            intervals.push_back(ValueInterval(0, 0, true, true, true, true));
        return ColumnPredicate(ColumnHandle(tbl, column_id_), std::move(intervals));
    }
    static vector<ValueInterval> organize(vector<SingleRangePred> single_preds) {
        if (single_preds.empty())
            return {};
        massert2(single_preds.size() == 1);
        massert2(single_preds.front().op().is_single_elem());
        return {ValueInterval(single_preds.front().value())};
    }
private:
    Metadata const& md_;
    const i64 column_id_;
    vector<SingleRangePred> single_preds_;
};
class TablePredicateBuilder {
public:
    TablePredicateBuilder(const Metadata& md) : md_(md) {
        for (auto const i : md_.columns())
            preds_.emplace_back(md, i);
    }
    template <class ...Ts>
    void add_pred(cname const& col, Ts && ...ts) {
        preds_[md_.column_id(col)].add_pred(ts...);
    }
    TablePredicate build(Table const& tbl) {
        vector<ColumnPredicate> preds;
        for (auto pred : preds_)
            preds.push_back(pred.build(tbl));
        return TablePredicate(md_, std::move(preds));
    }
private:
    const Metadata& md_;
    vector<RangePredBuilder> preds_;
}; // }}}
// parse {{{
Query parse(const Table& tbl, const std::string line) {
    TablePredicateBuilder where_builder(tbl.metadata());
    columns_t select_builder;
    // todo: select_builder: use metadata instead of table
    std::stringstream ss(line);
    std::string token;
    query_format_check(!ss.eof(), "empty line");
    ss >> token;
    query_format_check(token == "select", "no select at the beginning");
    {
        bool no_comma = true;
        while (no_comma && !ss.eof()) {
            ss >> token;
            if (token.back() != ',')
                no_comma = false;
            else
                token.pop_back();
            if (token != "*")
                select_builder.emplace_back(tbl, token);
            else
                for (auto const& id : tbl.metadata().columns())
                    select_builder.emplace_back(tbl, tbl.metadata().column_name(id));
        }
        query_format_check(!select_builder.empty(), "select list empty");
        query_format_check(!no_comma, "no comma after select list");
    }
    if (ss.eof()) {
        return Query{where_builder.build(tbl), std::move(select_builder)};
    }
    ss >> token;
    query_format_check(token == "where", "something else than 'where' after select list: " + token);
    {
        bool where_non_empty = false;
        bool no_comma = true;
        while (no_comma && !ss.eof()) {
            where_non_empty = true;
            ss >> token;
            if (token.back() != ',')
                no_comma = false;
            else
                token.pop_back();
            const auto sep_pos = token.find_first_of("=<>");
            query_format_check(sep_pos != std::string::npos, "<>= not found");
            const auto col = token.substr(0, sep_pos);
            const auto sep = PredOp(std::string(1, token[sep_pos]));
            const auto val = token.substr(sep_pos + 1, isize(token) - sep_pos - 1);
            where_builder.add_pred(col, sep, val);
        }
        query_format_check(where_non_empty, "where list empty");
        query_format_check(!no_comma, "no comma after where list");
    }
    if (ss.eof()) {
        return Query{where_builder.build(tbl), std::move(select_builder)};
    }
    ss >> token;
    throw query_format_error("there's something after 'where': " + token);
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
#ifndef NO_VALIDATION
    t.validate();
#endif
    std::string line;
    i64 count = 0;
    while (std::getline(std::cin, line)) {
        try {
            println();
            auto q = parse(tbl, line);
            println("query:", line);
            log_info("query:", line);
            dprintln(repr(q));
            Measure mes(std::to_string(++count));
            OutputFrame outp(std::cout);
            t.run(q, outp);
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
    log_info(msg, "- exiting");
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
