/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#include "Utility/GlobalMouseListener.h"

extern "C" {

int scalar_doclick(t_word* data, t_template* t, t_scalar* sc,
    t_array* ap, struct _glist* owner,
    t_float xloc, t_float yloc, int xpix, int ypix,
    int shift, int alt, int dbl, int doit);
}

#define CLOSED 1      /* polygon */
#define BEZ 2         /* bezier shape */
#define NOMOUSERUN 4  /* disable mouse interaction when in run mode  */
#define NOMOUSEEDIT 8 /* same in edit mode */
#define NOVERTICES 16 /* disable only vertex grabbing in run mode */
#define A_ARRAY 55    /* LATER decide whether to enshrine this in m_pd.h */

#define DRAWNUMBER_BUFSIZE 1024

// Global mouse listener class:
// If you attach a normal global mouse listener to a component on canvas, you run the risk of
// accidentally passing on mouse scroll events to the viewport.
// This prevents that with a separation layer.

class DrawableTemplate : public pd::MessageListener
    , public AsyncUpdater {

public:
    pd::Instance* pd;
    Canvas* canvas;
    t_float baseX, baseY;
    t_word* data;
    t_template* templ;
    t_template* parentTempl;
    pd::WeakReference scalar;

    DrawableTemplate(t_scalar* object, t_word* scalarData, t_template* scalarTemplate, t_template* parentTemplate, Canvas* cnv, t_float x, t_float y)
        : pd(cnv->pd)
        , canvas(cnv)
        , baseX(x)
        , baseY(y)
        , data(scalarData)
        , templ(scalarTemplate)
        , parentTempl(parentTemplate ? parentTemplate : scalarTemplate)
        , scalar(object, cnv->pd)
    {
        pd->registerMessageListener(scalar.getRawUnchecked<void>(), this);
        triggerAsyncUpdate();
    }

    ~DrawableTemplate()
    {
        pd->unregisterMessageListener(scalar.getRawUnchecked<void>(), this);
    }
        
    void receiveMessage(String const& name, int argc, t_atom* argv)
    {
        if (name == "redraw") {
            triggerAsyncUpdate();
        }
    }

    void handleAsyncUpdate()
    {
        update();
    }

    virtual void update() = 0;

    t_float xToPixels(t_float xval)
    {
        if (auto x = canvas->patch.getPointer()) {
            if (!getValue<bool>(canvas->isGraphChild))
                return (((xval - x->gl_x1)) / (x->gl_x2 - x->gl_x1));
            else if (getValue<bool>(canvas->isGraphChild) && !canvas->isGraph)
                return (x->gl_screenx2 - x->gl_screenx1) * (xval - x->gl_x1) / (x->gl_x2 - x->gl_x1);
            else {
                return (x->gl_pixwidth * (xval - x->gl_x1) / (x->gl_x2 - x->gl_x1)) + x->gl_xmargin;
            }
        }

        return xval;
    }

    t_float yToPixels(t_float yval)
    {
        if (auto x = canvas->patch.getPointer()) {
            if (!getValue<bool>(canvas->isGraphChild))
                return (((yval - x->gl_y1)) / (x->gl_y2 - x->gl_y1));
            else if (getValue<bool>(canvas->isGraphChild) && !canvas->isGraph)
                return (x->gl_screeny2 - x->gl_screeny1) * (yval - x->gl_y1) / (x->gl_y2 - x->gl_y1);
            else {
                return (x->gl_pixheight * (yval - x->gl_y1) / (x->gl_y2 - x->gl_y1)) + x->gl_ymargin;
            }
        }

        return yval;
    }

    /* getting and setting values via fielddescs -- note confusing names;
     the above are setting up the fielddesc itself. */
    static t_float fielddesc_getfloat(t_fake_fielddesc* f, t_template* templ, t_word* wp, int loud)
    {
        if (f->fd_type == A_FLOAT) {
            if (f->fd_var)
                return (template_getfloat(templ, f->fd_un.fd_varsym, wp, loud));
            else
                return (f->fd_un.fd_float);
        } else {
            return (0);
        }
    }

    static int rangecolor(int n) /* 0 to 9 in 5 steps */
    {
        int n2 = (n == 9 ? 8 : n); /* 0 to 8 */
        int ret = (n2 << 5);       /* 0 to 256 in 9 steps */
        if (ret > 255)
            ret = 255;
        return (ret);
    }

    static Colour numberToColour(int n)
    {
        auto rangecolor = [](int n) /* 0 to 9 in 5 steps */
        {
            int n2 = (n == 9 ? 8 : n); /* 0 to 8 */
            int ret = (n2 << 5);       /* 0 to 256 in 9 steps */
            if (ret > 255)
                ret = 255;
            return (ret);
        };

        if (n < 0)
            n = 0;

        int red = rangecolor(n / 100);
        int green = rangecolor((n / 10) % 10);
        int blue = rangecolor(n % 10);

        return Colour(red, green, blue);
    }
};

class DrawableCurve final : public DrawableTemplate
    , public DrawablePath {

    t_fake_curve* object;
    GlobalMouseListener globalMouseListener;
    Point<int> lastMouseDragPosition = {0, 0};

public:
    DrawableCurve(t_scalar* s, t_gobj* obj, t_word* data, t_template* templ, Canvas* cnv, int x, int y, t_template* parent = nullptr)
        : DrawableTemplate(s, data, templ, parent, cnv, x, y)
        , object(reinterpret_cast<t_fake_curve*>(obj))
        , globalMouseListener(cnv)
    {

        globalMouseListener.globalMouseDown = [this, cnv](const MouseEvent& e){
            auto localPos = e.getEventRelativeTo(this).getMouseDownPosition();
            if (!getLocalBounds().contains(localPos) || !getValue<bool>(canvas->locked) || !canvas->isShowing())
                return;
            
            if(auto gobj = scalar.get<t_gobj>()) {
                auto glist = cnv->patch.getPointer();
                auto pos = e.getPosition() - cnv->canvasOrigin;
                gobj_click(gobj.get(), glist.get(), pos.x, pos.y, e.mods.isShiftDown(), e.mods.isAltDown(), e.getNumberOfClicks() > 1, 1);
                cnv->updateDrawables();
                glist->gl_editor->e_xwas = pos.x;
                glist->gl_editor->e_ywas = pos.y;
            }
        };
        globalMouseListener.globalMouseUp = [this, cnv](const MouseEvent& e){
            auto localPos = e.getEventRelativeTo(this).getMouseDownPosition();
            if (!getLocalBounds().contains(localPos) || !getValue<bool>(canvas->locked) || !canvas->isShowing())
                return;
            
            if(auto gobj = scalar.get<t_gobj>()) {
                auto glist = cnv->patch.getPointer();
                auto pos = e.getPosition() - cnv->canvasOrigin;
                gobj_click(gobj.get(), glist.get(), pos.x, pos.y, e.mods.isShiftDown(), e.mods.isAltDown(), 0, 0);
                cnv->updateDrawables();
                glist->gl_editor->e_xwas = pos.x;
                glist->gl_editor->e_ywas = pos.y;
            }
        };
        globalMouseListener.globalMouseDrag = [this, cnv](const MouseEvent& e){
            auto localPos = e.getEventRelativeTo(this).getMouseDownPosition();
            if (!getLocalBounds().contains(localPos) || !getValue<bool>(canvas->locked) || !canvas->isShowing())
                return;
            
            if(auto gobj = scalar.get<t_gobj>()) {
                auto glist = cnv->patch.getPointer();
                auto pos = e.getPosition() - cnv->canvasOrigin;
                gobj_click(gobj.get(), glist.get(), pos.x, pos.y, e.mods.isShiftDown(), e.mods.isAltDown(), e.getNumberOfClicks() > 1, 1);
                
                auto* canvas = glist_getcanvas(glist.get());
                if(canvas->gl_editor->e_motionfn) {
                    canvas->gl_editor->e_motionfn(&canvas->gl_editor->e_grab->g_pd, pos.x - glist->gl_editor->e_xwas, pos.y - glist->gl_editor->e_ywas, 0);
                }
                
                cnv->updateDrawables();
                glist->gl_editor->e_xwas = pos.x;
                glist->gl_editor->e_ywas = pos.y;
            }
        };
        globalMouseListener.globalMouseMove = [this, cnv](const MouseEvent& e){
            auto localPos = e.getEventRelativeTo(this).getMouseDownPosition();
            if (!getLocalBounds().contains(localPos) || !getValue<bool>(canvas->locked) || !canvas->isShowing())
                return;
            
            if(auto gobj = scalar.get<t_gobj>()) {
                auto glist = cnv->patch.getPointer();
                auto pos = e.getPosition() - cnv->canvasOrigin;
                gobj_click(gobj.get(), glist.get(), pos.x, pos.y, e.mods.isShiftDown(), e.mods.isAltDown(), 0, 0);
                glist->gl_editor->e_xwas = pos.x;
                glist->gl_editor->e_ywas = pos.y;
            }
        };
    }

    void update() override
    {
        auto* s = scalar.getRaw<t_scalar>();

        if (!s || !s->sc_template)
            return;

        auto* glist = canvas->patch.getPointer().get();
        if (!glist)
            return;

        auto* x = reinterpret_cast<t_fake_curve*>(object);
        int n = x->x_npoints;

        if(parentTempl == templ) {
            scalar_getbasexy(s, &baseX, &baseY);
        }

        if (!fielddesc_getfloat(&x->x_vis, templ, data, 0)) {
            setPath(Path());
            return;
        }

        if (n > 1) {
            int flags = x->x_flags;
            int closed = flags & CLOSED;

            t_float width = fielddesc_getfloat(&x->x_width, templ, data, 1);

            int pix[200];
            if (n > 100)
                n = 100;

            canvas->pd->lockAudioThread();

            for (int i = 0; i < n; i++) {
                auto* f = x->x_vec + (i * 2);

                float xCoord = xToPixels(baseX + fielddesc_getcoord((t_fielddesc*)f, templ, data, 1));
                float yCoord = yToPixels(baseY + fielddesc_getcoord((t_fielddesc*)(f + 1), templ, data, 1));

                pix[2 * i] = xCoord + canvas->canvasOrigin.x;
                pix[2 * i + 1] = yCoord + canvas->canvasOrigin.y;
            }

            canvas->pd->unlockAudioThread();

            if (width < 1)
                width = 1;
            if (glist->gl_isgraph)
                width *= glist_getzoom(glist);

            auto strokeColour = numberToColour(fielddesc_getfloat(&x->x_outlinecolor, templ, data, 1));
            setStrokeFill(strokeColour);
            setStrokeThickness(width);

            if (closed) {
                auto fillColour = numberToColour(fielddesc_getfloat(&x->x_fillcolor, templ, data, 1));
                setFill(fillColour);
            } else {
                setFill(Colours::transparentBlack);
            }

            Path toDraw;

            toDraw.startNewSubPath(pix[0], pix[1]);
            
            if(flags & BEZ)
            {
                for (int i = 0; i < n; i++) {
                    float x0 = pix[2 * i];
                    float y0 = pix[2 * i + 1];
                    
                    float x1, y1;
                    if (i == n - 1) {
                        if(closed) {
                            x1 = pix[0];
                            y1 = pix[1];
                        }
                        else {
                            x1 = x0;
                            y1 = y0;
                        }
                    } else {
                        x1 = pix[2 * (i + 1)];
                        y1 = pix[2 * (i + 1) + 1];
                    }
                    toDraw.quadraticTo(x0, y0, (x0 + x1) / 2, (y0 + y1) / 2);
                    
                    if (i == n - 1) {
                        toDraw.quadraticTo((x0 + x1) / 2, (y0 + y1) / 2, x1, y1);
                    }
                }
            }
            else {
                for (int i = 1; i < n; i++) {
                    toDraw.lineTo(pix[2 * i], pix[2 * i + 1]);
                }
            }

            if (closed) {
                toDraw.lineTo(pix[0], pix[1]);
            }

            auto drawBounds = toDraw.getBounds();

            // tcl/tk will show a dot for a 0px polygon
            // JUCE doesn't do this, so we have to fake it
            if (closed && drawBounds.isEmpty()) {
                toDraw.clear();
                toDraw.addEllipse(drawBounds.withSizeKeepingCentre(5, 5));
                setStrokeThickness(2);
                setFill(getStrokeFill());
            }

            setPath(toDraw);
        } else {
            post("warning: curves need at least two points to be graphed");
        }
    }
};

class DrawableSymbol final : public DrawableTemplate
    , public DrawableText {

    t_fake_drawnumber* object;
    GlobalMouseListener mouseListener;

    float mouseDownValue;

public:
    DrawableSymbol(t_scalar* s, t_gobj* obj, t_word* data, t_template* templ, Canvas* cnv, int x, int y, t_template* parent = nullptr)
        : DrawableTemplate(s, data, templ, parent, cnv, x, y)
        , object(reinterpret_cast<t_fake_drawnumber*>(obj))
    {
        mouseListener.globalMouseDown = [this](MouseEvent const& e) {
            handleMouseDown(e.getEventRelativeTo(this));
        };
        mouseListener.globalMouseDrag = [this](MouseEvent const& e) {
            handleMouseDrag(e.getEventRelativeTo(this));
        };
    }

    void handleMouseDown(MouseEvent const& e)
    {
        if (!getLocalBounds().contains(e.getMouseDownPosition()) || !getValue<bool>(canvas->locked) || !canvas->isShowing())
            return;

        if (auto s = scalar.get<t_scalar>()) {
            int type, onset;
            t_symbol* arraytype;

            if (!s->sc_template || !template_find_field(templ, object->x_fieldname, &onset, &type, &arraytype) || type != DT_FLOAT) {
                return;
            }

            mouseDownValue = ((t_word*)((char*)data + onset))->w_float;
        }
    }

    void handleMouseDrag(MouseEvent const& e)
    {
        if (!getLocalBounds().contains(e.getMouseDownPosition()) || !getValue<bool>(canvas->locked) || !canvas->isShowing())
            return;

        if (auto s = scalar.get<t_scalar>()) {
            int type, onset;
            t_symbol* arraytype;

            if (!s->sc_template || !template_find_field(templ, object->x_fieldname, &onset, &type, &arraytype) || type != DT_FLOAT) {
                return;
            }

            ((t_word*)((char*)data + onset))->w_float = mouseDownValue - e.getDistanceFromDragStartY() / 6;
        }

        canvas->updateDrawables();
    }

    void update() override
    {
        auto* s = scalar.getRaw<t_scalar>();
        if (!s || !s->sc_template)
            return;

        auto* x = reinterpret_cast<t_fake_drawnumber*>(object);

        if (!fielddesc_getfloat(&x->x_vis, templ, data, 0)) {
            setText("");
            return;
        }
        
        int xloc = 0, yloc = 0;
        if (auto glist = canvas->patch.getPointer()) {
            xloc = xToPixels(baseX + fielddesc_getcoord((t_fielddesc*)&x->x_xloc, templ, data, 0)) + canvas->canvasOrigin.x;
            yloc = yToPixels(baseY + fielddesc_getcoord((t_fielddesc*)&x->x_yloc, templ, data, 0)) + canvas->canvasOrigin.y;
        }
        
        char buf[DRAWNUMBER_BUFSIZE];
        int type, onset;
        t_symbol* arraytype;

        if (!template_find_field(templ, x->x_fieldname, &onset, &type, &arraytype) || type == DT_ARRAY) {
            type = -1;
        }

        int nchars;
        if (type < 0)
            buf[0] = 0;
        else {
            strncpy(buf, x->x_label->s_name, DRAWNUMBER_BUFSIZE);
            buf[DRAWNUMBER_BUFSIZE - 1] = 0;
            nchars = (int)strlen(buf);
            if (type == DT_TEXT) {
                char* buf2;
                int size2, ncopy;
                binbuf_gettext(((t_word*)((char*)data + onset))->w_binbuf,
                    &buf2, &size2);
                ncopy = (size2 > DRAWNUMBER_BUFSIZE - 1 - nchars ? DRAWNUMBER_BUFSIZE - 1 - nchars : size2);
                memcpy(buf + nchars, buf2, ncopy);
                buf[nchars + ncopy] = 0;
                if (nchars + ncopy == DRAWNUMBER_BUFSIZE - 1)
                    strcpy(buf + (DRAWNUMBER_BUFSIZE - 4), "...");
                t_freebytes(buf2, size2);
            } else {
                t_atom at;
                if (type == DT_FLOAT)
                    SETFLOAT(&at, ((t_word*)((char*)data + onset))->w_float);
                else
                    SETSYMBOL(&at, ((t_word*)((char*)data + onset))->w_symbol);
                atom_string(&at, buf + nchars, DRAWNUMBER_BUFSIZE - nchars);
            }
        }

        auto symbolColour = numberToColour(fielddesc_getfloat(&x->x_color, templ, data, 1));
        setColour(symbolColour);
        auto text = String::fromUTF8(buf);
        auto font = getFont();

        setBoundingBox(Parallelogram<float>(Rectangle<float>(xloc, yloc, font.getStringWidthFloat(text) + 4.0f, font.getHeight() + 4.0f)));
        if (auto glist = canvas->patch.getPointer()) {
            setFontHeight(sys_hostfontsize(glist_getfont(glist.get()), glist_getzoom(glist.get())));
        }
        setJustification(Justification::topLeft);
        setText(text);
    }
};

class DrawablePlot final : public DrawableTemplate
    , public DrawablePath {


    Point<int> lastMouseDragPosition = {0, 0};
    t_fake_curve* object;
    GlobalMouseListener globalMouseListener;
        
public:
    DrawablePlot(t_scalar* s, t_gobj* obj, t_word* data, t_template* templ, Canvas* cnv, int x, int y, t_template* parent = nullptr)
        : DrawableTemplate(s, data, templ, parent, cnv, x, y)
        , object(reinterpret_cast<t_fake_curve*>(obj))
        , globalMouseListener(cnv)
    {
        /* TODO: finish this and enable it!
        globalMouseListener.globalMouseDown = [this, cnv](const MouseEvent& e){
            auto localPos = e.getEventRelativeTo(this).getMouseDownPosition();
            if (!getLocalBounds().contains(localPos) || !getValue<bool>(canvas->locked) || !canvas->isShowing())
                return;
            
            if(auto gobj = scalar.get<t_gobj>()) {
                auto glist = cnv->patch.getPointer();
                auto pos = e.getPosition() - cnv->canvasOrigin;
                gobj_click(gobj.get(), glist.get(), pos.x, pos.y, e.mods.isShiftDown(), e.mods.isAltDown(), e.getNumberOfClicks() > 1, 1);
                cnv->updateDrawables();
                glist->gl_editor->e_xwas = pos.x;
                glist->gl_editor->e_ywas = pos.y;
            }
        };
        globalMouseListener.globalMouseUp = [this, cnv](const MouseEvent& e){
            auto localPos = e.getEventRelativeTo(this).getMouseDownPosition();
            if (!getLocalBounds().contains(localPos) || !getValue<bool>(canvas->locked) || !canvas->isShowing())
                return;
            
            if(auto gobj = scalar.get<t_gobj>()) {
                auto glist = cnv->patch.getPointer();
                auto pos = e.getPosition() - cnv->canvasOrigin;
                gobj_click(gobj.get(), glist.get(), pos.x, pos.y, e.mods.isShiftDown(), e.mods.isAltDown(), 0, 0);
                cnv->updateDrawables();
                glist->gl_editor->e_xwas = pos.x;
                glist->gl_editor->e_ywas = pos.y;
            }
        };
        globalMouseListener.globalMouseDrag = [this, cnv](const MouseEvent& e){
            auto localPos = e.getEventRelativeTo(this).getMouseDownPosition();
            if (!getLocalBounds().contains(localPos) || !getValue<bool>(canvas->locked) || !canvas->isShowing())
                return;
            
            if(auto gobj = scalar.get<t_gobj>()) {
                auto glist = cnv->patch.getPointer();
                auto pos = e.getPosition() - cnv->canvasOrigin;
                gobj_click(gobj.get(), glist.get(), pos.x, pos.y, e.mods.isShiftDown(), e.mods.isAltDown(), e.getNumberOfClicks() > 1, 1);
                
                auto* canvas = glist_getcanvas(glist.get());
                if(canvas->gl_editor->e_motionfn) {
                    canvas->gl_editor->e_motionfn(&canvas->gl_editor->e_grab->g_pd, pos.x - glist->gl_editor->e_xwas, pos.y - glist->gl_editor->e_ywas, 0);
                }
                
                cnv->updateDrawables();
                glist->gl_editor->e_xwas = pos.x;
                glist->gl_editor->e_ywas = pos.y;
            }
        };
        globalMouseListener.globalMouseMove = [this, cnv](const MouseEvent& e){
            auto localPos = e.getEventRelativeTo(this).getMouseDownPosition();
            if (!getLocalBounds().contains(localPos) || !getValue<bool>(canvas->locked) || !canvas->isShowing())
                return;
            
            if(auto gobj = scalar.get<t_gobj>()) {
                auto glist = cnv->patch.getPointer();
                auto pos = e.getPosition() - cnv->canvasOrigin;
                gobj_click(gobj.get(), glist.get(), pos.x, pos.y, e.mods.isShiftDown(), e.mods.isAltDown(), 0, 0);
                glist->gl_editor->e_xwas = pos.x;
                glist->gl_editor->e_ywas = pos.y;
            }
        }; */
    }

    static int readOwnerTemplate(t_fake_plot* x,
        t_word* data, t_template* ownertemplate,
        t_symbol** elemtemplatesymp, t_array** arrayp,
        t_float* linewidthp, t_float* xlocp, t_float* xincp, t_float* ylocp,
        t_float* stylep, t_float* visp, t_float* scalarvisp, t_float* editp,
        t_fake_fielddesc** xfield, t_fake_fielddesc** yfield, t_fake_fielddesc** wfield)
    {
        int arrayonset, type;
        t_symbol* elemtemplatesym;
        t_array* array;

        /* find the data and verify it's an array */
        if (x->x_data.fd_type != A_ARRAY || !x->x_data.fd_var) {
            pd_error(0, "plot: needs an array field");
            return (-1);
        }
        if (!template_find_field(ownertemplate, x->x_data.fd_un.fd_varsym,
                &arrayonset, &type, &elemtemplatesym)) {
            pd_error(0, "plot: %s: no such field", x->x_data.fd_un.fd_varsym->s_name);
            return (-1);
        }
        if (type != DT_ARRAY) {
            pd_error(0, "plot: %s: not an array", x->x_data.fd_un.fd_varsym->s_name);
            return (-1);
        }
        array = *(t_array**)(((char*)data) + arrayonset);
        *linewidthp = fielddesc_getfloat(&x->x_width, ownertemplate, data, 1);
        *xlocp = fielddesc_getfloat(&x->x_xloc, ownertemplate, data, 1);
        *xincp = fielddesc_getfloat(&x->x_xinc, ownertemplate, data, 1);
        *ylocp = fielddesc_getfloat(&x->x_yloc, ownertemplate, data, 1);
        *stylep = fielddesc_getfloat(&x->x_style, ownertemplate, data, 1);
        *visp = fielddesc_getfloat(&x->x_vis, ownertemplate, data, 1);
        *scalarvisp = fielddesc_getfloat(&x->x_scalarvis, ownertemplate, data, 1);
        *editp = fielddesc_getfloat(&x->x_edit, ownertemplate, data, 1);
        *elemtemplatesymp = elemtemplatesym;
        *arrayp = array;
        *xfield = &x->x_xpoints;
        *yfield = &x->x_ypoints;
        *wfield = &x->x_wpoints;
        return (0);
    }

    Array<Component*> getSubPlots()
    {
        auto* s = scalar.getRaw<t_scalar>();

        if (!s || !s->sc_template)
            return {};

        auto* glist = canvas->patch.getPointer().get();
        if (!glist)
            return {};

        auto* x = reinterpret_cast<t_fake_plot*>(object);
        int elemsize, yonset, wonset, xonset, i;
        t_canvas* elemtemplatecanvas;
        t_template* elemtemplate;
        t_symbol* elemtemplatesym;
        t_float linewidth, xloc, xinc, yloc, style, yval,
            vis, scalarvis, edit;
        double xsum;
        t_array* array;
        t_fake_fielddesc *xfielddesc, *yfielddesc, *wfielddesc;

        if (readOwnerTemplate(x, data, templ,
                &elemtemplatesym, &array, &linewidth, &xloc, &xinc, &yloc, &style,
                &vis, &scalarvis, &edit, &xfielddesc, &yfielddesc, &wfielddesc)
            || array_getfields(elemtemplatesym, &elemtemplatecanvas,
                &elemtemplate, &elemsize, (t_fielddesc*)xfielddesc, (t_fielddesc*)yfielddesc, (t_fielddesc*)wfielddesc,
                &xonset, &yonset, &wonset))
            return {};

        int nelem = array->a_n;
        auto* elem = (char*)array->a_vec;

        Array<Component*> drawables;

        for (xsum = xloc, i = 0; i < nelem; i++) {
            t_float usexloc, useyloc;

            if (xonset >= 0)
                usexloc = baseX + xloc + *(t_float*)((elem + elemsize * i) + xonset);
            else
                usexloc = baseX + xsum, xsum += xinc;
            if (yonset >= 0)
                yval = *(t_float*)((elem + elemsize * i) + yonset);
            else
                yval = 0;
            useyloc = baseY + yloc + fielddesc_cvttocoord((t_fielddesc*)yfielddesc, yval);
            auto* subData = (t_word*)(elem + elemsize * i);

            for (auto* y = elemtemplatecanvas->gl_list; y; y = y->g_next) {
                t_parentwidgetbehavior const* wb = pd_getparentwidget(&y->g_pd);
                if (!wb)
                    continue;

                auto name = String::fromUTF8(y->g_pd->c_name->s_name);
                if (name == "drawtext" || name == "drawnumber" || name == "drawsymbol") {
                    drawables.add(new DrawableSymbol(s, y, subData, elemtemplate, canvas, static_cast<int>(usexloc), static_cast<int>(useyloc), templ));
                } else if (name == "drawpolygon" || name == "drawcurve" || name == "filledpolygon" || name == "filledcurve") {
                    drawables.add(new DrawableCurve(s, y, subData, elemtemplate, canvas, static_cast<int>(usexloc), static_cast<int>(useyloc), templ));
                } else if (name == "plot") {
                    drawables.add(new DrawablePlot(s, y, subData, elemtemplate, canvas, static_cast<int>(usexloc), static_cast<int>(useyloc), templ));
                }
            }
        }

        return drawables;
    }

    void update() override
    {
        auto* s = scalar.getRaw<t_scalar>();

        if (!s || !s->sc_template)
            return;

        auto* glist = canvas->patch.getPointer().get();
        if (!glist)
            return;

        auto* x = reinterpret_cast<t_fake_plot*>(object);
        int elemsize, yonset, wonset, xonset, i;
        t_canvas* elemtemplatecanvas;
        t_template* elemtemplate;
        t_symbol* elemtemplatesym;
        t_float linewidth, xloc, xinc, yloc, style,
            vis, scalarvis, edit;
        double xsum;
        t_array* array;
        int nelem;
        char* elem;
        t_fake_fielddesc *xfielddesc, *yfielddesc, *wfielddesc;

        /* even if the array is "invisible", if its visibility is
         set by an instance variable you have to explicitly erase it,
         because the flag could earlier have been on when we were getting
         drawn.  Rather than look to try to find out whether we're
         visible we just do the erasure.  At the TK level this should
         cause no action because the tag matches nobody.  LATER we
         might want to optimize this somehow.  Ditto the "vis()" routines
         for other drawing instructions. */

        if (readOwnerTemplate(x, data, templ,
                &elemtemplatesym, &array, &linewidth, &xloc, &xinc, &yloc, &style,
                &vis, &scalarvis, &edit, &xfielddesc, &yfielddesc, &wfielddesc)
            || array_getfields(elemtemplatesym, &elemtemplatecanvas,
                &elemtemplate, &elemsize, (t_fielddesc*)xfielddesc, (t_fielddesc*)yfielddesc, (t_fielddesc*)wfielddesc,
                &xonset, &yonset, &wonset))
            return;

        nelem = array->a_n;
        elem = (char*)array->a_vec;

        if (glist->gl_isgraph)
            linewidth *= glist_getzoom(glist);

        setStrokeThickness(linewidth);

        t_float coordinates[1024 * 2];

        Path toDraw;

        if (static_cast<int>(style) == PLOTSTYLE_POINTS) {
            t_float minyval = 1e20, maxyval = -1e20;
            int ndrawn = 0;
            Colour colour = numberToColour(fielddesc_getfloat(&x->x_outlinecolor, templ, data, 1));

            setStrokeFill(Colours::transparentBlack);
            setFill(colour);

            for (xsum = baseX + xloc, i = 0; i < nelem; i++) {
                t_float yval, usexloc;
                int ixpix, inextx;

                if (xonset >= 0) {
                    usexloc = baseX + xloc + *(t_float*)((elem + elemsize * i) + xonset);
                    ixpix = xToPixels(fielddesc_cvttocoord((t_fielddesc*)xfielddesc, usexloc));
                    inextx = ixpix + 2;
                } else {
                    usexloc = xsum;
                    xsum += xinc;
                    ixpix = xToPixels(fielddesc_cvttocoord((t_fielddesc*)xfielddesc, usexloc));
                    inextx = xToPixels(fielddesc_cvttocoord((t_fielddesc*)xfielddesc, xsum));
                }

                if (yonset >= 0)
                    yval = yloc + *(t_float*)((elem + elemsize * i) + yonset);
                else
                    yval = 0;
                yval = std::clamp<float>(yval, -1e20, 1e20);
                if (yval < minyval)
                    minyval = yval;
                if (yval > maxyval)
                    maxyval = yval;
                if (i == nelem - 1 || inextx != ixpix) {

                    toDraw.addRectangle(ixpix, yToPixels(baseY + fielddesc_cvttocoord((t_fielddesc*)yfielddesc, minyval)), inextx, yToPixels(baseY + fielddesc_cvttocoord((t_fielddesc*)yfielddesc, maxyval)) + linewidth);

                    ndrawn++;
                    minyval = 1e20;
                    maxyval = -1e20;
                }
                if (ndrawn > 2000)
                    break;
            }
        } else {
            Colour outline = numberToColour(
                fielddesc_getfloat(&x->x_outlinecolor, templ, data, 1));

            setStrokeFill(outline);
            setFill(Colours::transparentBlack);

            int lastpixel = -1, ndrawn = 0;
            t_float yval = 0, wval = 0, xpix;
            int ixpix = 0;
            /* draw the trace */

            if (wonset >= 0) {
                /* found "w" field which controls linewidth.  The trace is
                 a filled polygon with 2n points. */

                setFill(outline);
                for (i = 0, xsum = xloc; i < nelem; i++) {
                    t_float usexloc;
                    if (xonset >= 0)
                        usexloc = xloc + *(t_float*)((elem + elemsize * i) + xonset);
                    else
                        usexloc = xsum, xsum += xinc;
                    if (yonset >= 0)
                        yval = *(t_float*)((elem + elemsize * i) + yonset);
                    else
                        yval = 0;
                    yval = std::clamp<float>(yval, -1e20, 1e20);
                    wval = *(t_float*)((elem + elemsize * i) + wonset);
                    wval = std::clamp<float>(wval, -1e20, 1e20);
                    xpix = xToPixels(baseX + fielddesc_cvttocoord((t_fielddesc*)xfielddesc, usexloc));
                    ixpix = xpix + 0.5;
                    if (xonset >= 0 || ixpix != lastpixel) {
                        coordinates[ndrawn * 2 + 0] = ixpix;
                        coordinates[ndrawn * 2 + 1] = yToPixels(baseY + yloc + fielddesc_cvttocoord((t_fielddesc*)yfielddesc, yval) - fielddesc_cvttocoord((t_fielddesc*)wfielddesc, wval));
                        ndrawn++;
                    }
                    lastpixel = ixpix;
                    if (ndrawn * 2 >= sizeof(coordinates) / sizeof(*coordinates))
                        goto ouch;
                }
                lastpixel = -1;
                for (i = nelem - 1; i >= 0; i--) {
                    t_float usexloc;
                    if (xonset >= 0)
                        usexloc = xloc + *(t_float*)((elem + elemsize * i) + xonset);
                    else
                        xsum -= xinc, usexloc = xsum;
                    if (yonset >= 0)
                        yval = *(t_float*)((elem + elemsize * i) + yonset);
                    else
                        yval = 0;
                    yval = std::clamp<float>(yval, -1e20, 1e20);
                    wval = *(t_float*)((elem + elemsize * i) + wonset);
                    wval = std::clamp<float>(wval, -1e20, 1e20);
                    xpix = xToPixels(baseX + fielddesc_cvttocoord((t_fielddesc*)xfielddesc, usexloc));
                    ixpix = xpix + 0.5;
                    if (xonset >= 0 || ixpix != lastpixel) {
                        coordinates[ndrawn * 2 + 0] = ixpix;
                        coordinates[ndrawn * 2 + 1] = yToPixels(baseY + yloc + fielddesc_cvttocoord((t_fielddesc*)yfielddesc, yval) + fielddesc_cvttocoord((t_fielddesc*)wfielddesc, wval));
                        ndrawn++;
                    }
                    lastpixel = ixpix;
                    if (ndrawn * 2 >= sizeof(coordinates) / sizeof(*coordinates))
                        goto ouch;
                }

                /* TK will complain if there aren't at least 3 points.
                 There should be at least two already. */
                if (ndrawn < 4) {
                    coordinates[ndrawn * 2 + 0] = ixpix + 10;
                    coordinates[ndrawn * 2 + 1] = yToPixels(baseY + yloc + fielddesc_cvttocoord((t_fielddesc*)yfielddesc, yval) - fielddesc_cvttocoord((t_fielddesc*)wfielddesc, wval));
                    ndrawn++;

                    coordinates[ndrawn * 2 + 0] = ixpix + 10;
                    coordinates[ndrawn * 2 + 1] = yToPixels(baseY + yloc + fielddesc_cvttocoord((t_fielddesc*)yfielddesc, yval) + fielddesc_cvttocoord((t_fielddesc*)wfielddesc, wval));
                    ndrawn++;
                }
            ouch:

                if (style == PLOTSTYLE_BEZ) {
                    float startX = coordinates[0] + canvas->canvasOrigin.x;
                    float startY = coordinates[1] + canvas->canvasOrigin.y;

                    toDraw.startNewSubPath(startX, startY);

                    for (int i = 0; i < ndrawn; i++) {
                        float x0 = coordinates[2 * i] + canvas->canvasOrigin.x;
                        float y0 = coordinates[2 * i + 1] + canvas->canvasOrigin.y;

                        float x1, y1;
                        if (i == ndrawn - 1) {
                            x1 = startX;
                            y1 = startY;
                        } else {
                            x1 = coordinates[2 * (i + 1)] + canvas->canvasOrigin.x;
                            y1 = coordinates[2 * (i + 1) + 1] + canvas->canvasOrigin.y;
                        }

                        toDraw.quadraticTo(x0, y0, (x0 + x1) / 2, (y0 + y1) / 2);

                        if (i == ndrawn - 1) {
                            toDraw.quadraticTo((x0 + x1) / 2, (y0 + y1) / 2, x1, y1);
                        }
                    }

                    toDraw.closeSubPath();
                    toDraw = toDraw.createPathWithRoundedCorners(6.0f);
                } else {
                    toDraw.startNewSubPath(coordinates[0] + canvas->canvasOrigin.x, coordinates[1] + canvas->canvasOrigin.y);
                    for (int i = 1; i < ndrawn; i++) {
                        toDraw.lineTo(coordinates[2 * i] + canvas->canvasOrigin.x, coordinates[2 * i + 1] + canvas->canvasOrigin.y);
                    }
                    toDraw.lineTo(coordinates[0] + canvas->canvasOrigin.x, coordinates[1] + canvas->canvasOrigin.y);
                }
            } else if (linewidth > 0) {
                /* no "w" field.  If the linewidth is positive, draw a
                 segmented line with the requested width; otherwise don't
                 draw the trace at all. */
                for (i = 0, xsum = xloc; i < nelem; i++) {
                    t_float usexloc;
                    if (xonset >= 0)
                        usexloc = xloc + *(t_float*)((elem + elemsize * i) + xonset);
                    else
                        usexloc = xsum, xsum += xinc;
                    if (yonset >= 0)
                        yval = *(t_float*)((elem + elemsize * i) + yonset);
                    else
                        yval = 0;
                    yval = std::clamp<float>(yval, -1e20, 1e20);

                    xpix = xToPixels(baseX + fielddesc_cvttocoord((t_fielddesc*)xfielddesc, usexloc));
                    ixpix = xpix + 0.5;
                    if (xonset >= 0 || ixpix != lastpixel) {
                        coordinates[ndrawn * 2 + 0] = ixpix;
                        coordinates[ndrawn * 2 + 1] = yToPixels(baseY + yloc + fielddesc_cvttocoord((t_fielddesc*)yfielddesc, yval));
                        ndrawn++;
                    }
                    lastpixel = ixpix;
                    if (ndrawn * 2 >= sizeof(coordinates) / sizeof(*coordinates))
                        break;
                }

                /* TK will complain if there aren't at least 2 points...
                   Don't know about JUCE though...
                 */
                if (ndrawn == 1) {
                    coordinates[2] = ixpix + 10;
                    coordinates[3] = yToPixels(baseY + yloc + fielddesc_cvttocoord((t_fielddesc*)yfielddesc, yval));
                    ndrawn = 2;
                }

                if (ndrawn) {
                    toDraw.startNewSubPath(coordinates[0] + canvas->canvasOrigin.x, coordinates[1] + canvas->canvasOrigin.y);
                    for (int i = 1; i < ndrawn; i++) {
                        toDraw.lineTo(coordinates[2 * i] + canvas->canvasOrigin.x, coordinates[2 * i + 1] + canvas->canvasOrigin.y);
                    }
                }
            }
        }

        setPath(toDraw);
    }
};

struct ScalarObject final : public ObjectBase {
    OwnedArray<Component> templates;
    
    ScalarObject(t_gobj* obj, Object* object)
        : ObjectBase(obj, object)
    {
        cnv->pd->setThis();

        // Make object invisible
        object->setVisible(false);

        auto* x = reinterpret_cast<t_scalar*>(obj);
        auto* templ = template_findbyname(x->sc_template);
        auto* templatecanvas = template_findcanvas(templ);
        t_float baseX, baseY;
        scalar_getbasexy(x, &baseX, &baseY);
        auto* data = x->sc_vec;
        for (auto* y = templatecanvas->gl_list; y; y = y->g_next) {
            t_parentwidgetbehavior const* wb = pd_getparentwidget(&y->g_pd);
            if (!wb)
                continue;

            auto name = String::fromUTF8(y->g_pd->c_name->s_name);
            
            if (name == "drawtext" || name == "drawnumber" || name == "drawsymbol") {
                cnv->addAndMakeVisible(templates.add(new DrawableSymbol(x, y, data, templ, cnv, static_cast<int>(baseX), static_cast<int>(baseY))));
            } else if (name == "drawpolygon" || name == "drawcurve" || name == "filledpolygon" || name == "filledcurve") {
                cnv->addAndMakeVisible(templates.add(new DrawableCurve(x, y, data, templ, cnv, static_cast<int>(baseX), static_cast<int>(baseY))));
            } else if (name == "plot") {
                auto* plot = new DrawablePlot(x, y, data, templ, cnv, static_cast<int>(baseX), static_cast<int>(baseY));
                cnv->addAndMakeVisible(templates.add(plot));

                for (auto* subplot : plot->getSubPlots()) {
                    cnv->addAndMakeVisible(templates.add(subplot));
                }
            }
        }
        
        for(int i = templates.size() - 1; i >= 0; i--)
        {
            templates[i]->toBack();
        }
        

        updateDrawables();
    }

    ~ScalarObject() override
    {
        for (auto* drawable : templates) {
            cnv->removeChildComponent(drawable);
        }
    }

    void updateDrawables() override
    {
        pd->setThis();

        for (auto* drawable : templates) {
            dynamic_cast<DrawableTemplate*>(drawable)->triggerAsyncUpdate();
        }
    }

    Rectangle<int> getPdBounds() override { return { 0, 0, 0, 0 }; }

    void setPdBounds(Rectangle<int> b) override { }
};
