#pragma once

#define DISCOVERY_TEST_FIELD(name) int name

namespace demo {
struct Note {
    /// Stable note identifier used by the application.
    int id;
    DISCOVERY_TEST_FIELD(macro_member);
    static int opened;
    /// Archive the note after receiving an application-defined reason.
    bool archive(int reason) const;
private:
    int flags;
protected:
    void invalidate();
};

struct Base { int inherited; };
class Project : public Base {
public:
    Note note;
    int size() const;
    void add(int id);
    void add(const char* text);
private:
    int secret;
#ifdef BUILD_DISCOVERY_TEST_EXTRA
    int conditional_member;
#endif
};
template<class T> class Box {
public:
    T value;
    void set(T next);
};
enum class Color { red, blue };
extern Project active;
}
