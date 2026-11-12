#include "app.h"

struct JournalPage : Page {
    JournalPage() : Page(PG_JOURNAL, L"Откат и журнал", L"Все изменения, сделанные мастером, и откат каждого из них", Ico::Undo) {}

    wstring NavBadge(Sev& s) override {
        int n = 0;
        for (auto& e : Journal::All()) if (!e.undone && !e.undoKind.empty()) ++n;
        s = Sev::Info;
        return n ? std::to_wstring(n) : L"";
    }

    void DoUndo(uint64_t id) {
        if (GuardBusy()) return;
        JEntry* e = Journal::Find(id);
        if (!e || e->undone) return;
        JEntry copy = *e;
        Confirm(L"Откатить это изменение?",
                copy.title + (copy.detail.empty() ? L"" : L"\n" + copy.detail) + L"\n\nИзменение будет отменено.",
                L"Откатить", [copy] {
            if (GuardBusy()) return;
            StartTask(L"Откат: " + copy.title, PG_JOURNAL, [copy](TaskCtx& c) {
                wstring err;
                if (Journal::Undo(copy, err)) { Journal::MarkUndone(copy.id); Journal::Save(); c.Done(L"откат выполнен"); }
                else c.Fail(err.empty() ? L"не удалось откатить" : err);
            });
        }, false, Sev::Info);
    }

    float Body(float x, float y, float w) override {
        auto& all = Journal::All();
        y += SectionTitle(x, y, w, L"Журнал изменений",
                          L"Здесь перечислено всё, что программа меняла в системе. Многие действия можно вернуть обратно кнопкой «Откатить».");
        if (all.empty()) {
            y += Banner(x, y, w, Sev::Ok, L"Журнал пуст", L"Мастер ещё ничего не менял на этом компьютере.") + 12;
            return y + 10;
        }
        for (auto it = all.rbegin(); it != all.rend(); ++it) {
            JEntry& e = *it;
            RectF r(x, y, w, 64);
            bool hv = Hover(r);
            FillRound(r, 10, hv ? P.surfaceHover : P.surface);
            if (e.thisSession && !e.undone) GradientH(RectF(r.X, r.Y + 10, 3, r.Height - 20), 1.5f, P.accent, P.accent2);
            Text(e.title, RectF(r.X + 18, r.Y + 10, w - 180, 20), FontUI(13.5f, 1), e.undone ? P.muted : P.text, T_LEFT | T_VCENTER | T_ELLIPSIS);
            wstring sub = e.time + (e.detail.empty() ? L"" : L"  ·  " + e.detail);
            Text(sub, RectF(r.X + 18, r.Y + 34, w - 180, 18), FontUI(12), P.muted, T_LEFT | T_VCENTER | T_ELLIPSIS);
            if (e.undone) {
                Badge(r.GetRight() - 16, r.Y + 20, L"откачено", Sev::Ok, 24, true);
            } else if (!e.undoKind.empty()) {
                uint64_t id = e.id;
                Button(RectF(r.GetRight() - 130, r.Y + 16, 114, 32), L"Откатить", [this, id] { DoUndo(id); }, Btn::Secondary, Ico::Undo, !TaskBusy());
            }
            y += 70;
        }
        float ch = ConsoleBox(x, y, w, 220, PG_JOURNAL);
        if (ch) y += ch + 16;
        return y + 10;
    }
};

Page* MakeJournal() { return new JournalPage; }
