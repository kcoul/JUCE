namespace juce
{

namespace
{
    class QnxTypeface final : public Typeface
    {
    public:
        QnxTypeface (String familyName, String styleName)
            : Typeface (familyName, styleName),
              native (createNative())
        {
        }

        Typeface::Ptr createSystemFallback (const String&, const String&) const override
        {
            return const_cast<QnxTypeface*> (this);
        }

        const Native* getNativeDetails() const override
        {
            return &native;
        }

    private:
        static Native createNative()
        {
            TypefaceNativeOptions options;
            options.font = HbFont { hb_font_create (hb_face_get_empty()), IncrementRef::no };
            options.metrics.ascent = 0.8f;
            options.metrics.descent = 0.2f;
            return Native { std::move (options) };
        }

        Native native;
    };

    Typeface::Ptr makeTypeface (const String& familyName, const String& styleName)
    {
        return new QnxTypeface (familyName.isNotEmpty() ? familyName : Font::getDefaultSansSerifFontName(),
                                styleName.isNotEmpty() ? styleName : Font::getDefaultStyle());
    }
}

StringArray Font::findAllTypefaceNames()
{
    return { getDefaultSansSerifFontName() };
}

StringArray Font::findAllTypefaceStyles (const String&)
{
    return { getDefaultStyle() };
}

Typeface::Ptr Typeface::createSystemTypefaceFor (const Font& font)
{
    return makeTypeface (font.getTypefaceName(), font.getTypefaceStyle());
}

Typeface::Ptr Typeface::createSystemTypefaceFor (Span<const std::byte>)
{
    return makeTypeface ("MemoryFont", Font::getDefaultStyle());
}

Typeface::Ptr Typeface::findSystemTypeface()
{
    return makeTypeface (Font::getDefaultSansSerifFontName(), Font::getDefaultStyle());
}

Typeface::Ptr Font::Native::getDefaultPlatformTypefaceForFont (const Font& font)
{
    return Typeface::createSystemTypefaceFor (font);
}

} // namespace juce
