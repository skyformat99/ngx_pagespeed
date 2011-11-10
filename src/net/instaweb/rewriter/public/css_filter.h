/*
 * Copyright 2010 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Author: sligocki@google.com (Shawn Ligocki)

#ifndef NET_INSTAWEB_REWRITER_PUBLIC_CSS_FILTER_H_
#define NET_INSTAWEB_REWRITER_PUBLIC_CSS_FILTER_H_

#include <vector>

#include "base/scoped_ptr.h"
#include "net/instaweb/htmlparse/public/html_element.h"
#include "net/instaweb/rewriter/public/css_resource_slot.h"
#include "net/instaweb/rewriter/public/output_resource_kind.h"
#include "net/instaweb/rewriter/public/resource.h"
#include "net/instaweb/rewriter/public/resource_combiner.h"
#include "net/instaweb/rewriter/public/resource_manager.h"
#include "net/instaweb/rewriter/public/rewrite_driver.h"
#include "net/instaweb/rewriter/public/rewrite_options.h"
#include "net/instaweb/rewriter/public/rewrite_single_resource_filter.h"
#include "net/instaweb/rewriter/public/single_rewrite_context.h"
#include "net/instaweb/util/public/basictypes.h"
#include "net/instaweb/util/public/google_url.h"
#include "net/instaweb/util/public/string.h"
#include "net/instaweb/util/public/string_util.h"

namespace Css {

class Stylesheet;

}  // namespace Css

namespace net_instaweb {

class CssImageRewriter;
class CssImageRewriterAsync;
class CacheExtender;
class HtmlCharactersNode;
class ImageCombineFilter;
class ImageRewriteFilter;
class MessageHandler;
class OutputPartitions;
class RewriteContext;
class Statistics;
class Variable;

// Find and parse all CSS in the page and apply transformations including:
// minification, combining, refactoring, and optimizing sub-resources.
//
// Currently only does basic minification.
//
// Note that CssCombineFilter currently does combining (although there is a bug)
// but CssFilter will eventually replace this.
//
// Currently only deals with inline <style> tags and external <link> resources.
// It does not consider style= attributes on arbitrary elements.
class CssFilter : public RewriteSingleResourceFilter {
 public:
  class Context;

  CssFilter(RewriteDriver* driver, const StringPiece& filter_prefix,
            // TODO(sligocki): Temporary pattern until we figure out a better
            // way to do this without passing all filters around everywhere.
            CacheExtender* cache_extender,
            ImageRewriteFilter* image_rewriter,
            ImageCombineFilter* image_combiner);
  virtual ~CssFilter();

  static void Initialize(Statistics* statistics);
  static void Terminate();

  // Note: AtExitManager needs to be initialized or you get a nasty error:
  // Check failed: false. Tried to RegisterCallback without an AtExitManager.
  // This is called by Initialize.
  static void InitializeAtExitManager();

  virtual void StartDocumentImpl();
  virtual void StartElementImpl(HtmlElement* element);
  virtual void Characters(HtmlCharactersNode* characters);
  virtual void EndElementImpl(HtmlElement* element);

  virtual const char* Name() const { return "CssFilter"; }
  virtual int FilterCacheFormatVersion() const;

  static const char kFilesMinified[];
  static const char kMinifiedBytesSaved[];
  static const char kParseFailures[];

 protected:
  virtual bool HasAsyncFlow() const;
  virtual RewriteContext* MakeRewriteContext();

 private:
  friend class Context;
  Context* MakeContext();

  TimedBool RewriteCssText(Context* context,
                           const GoogleUrl& css_base_gurl,
                           const GoogleUrl& css_trim_gurl,
                           const StringPiece& in_text,
                           bool text_is_declarations,
                           GoogleString* out_text,
                           MessageHandler* handler);
  bool RewriteExternalCss(const StringPiece& in_url, GoogleString* out_url);

  // Tries to write out a (potentially edited) stylesheet out to out_text,
  // and returns whether we should consider the result as an improvement.
  bool SerializeCss(RewriteContext* context,
                    int64 in_text_size,
                    const Css::Stylesheet* stylesheet,
                    const GoogleUrl& css_base_gurl,
                    const GoogleUrl& css_trim_gurl,
                    bool previously_optimized,
                    bool stylesheet_is_declarations,
                    GoogleString* out_text,
                    MessageHandler* handler);

  virtual RewriteResult RewriteLoadedResource(
      const ResourcePtr& input_resource,
      const OutputResourcePtr& output_resource);

  Css::Stylesheet* CombineStylesheets(
      std::vector<Css::Stylesheet*>* stylesheets);
  bool LoadAllSubStylesheets(Css::Stylesheet* stylesheet_with_imports,
                             std::vector<Css::Stylesheet*>* result_stylesheets);

  Css::Stylesheet* LoadStylesheet(const StringPiece& url) { return NULL; }

  bool in_style_element_;  // Are we in a style element?
  // These are meaningless if in_style_element_ is false:
  HtmlElement* style_element_;  // The element we are in.
  HtmlCharactersNode* style_char_node_;  // The single character node in style.

  scoped_ptr<CssImageRewriter> image_rewriter_;

  // Filters we delegate to.
  CacheExtender* cache_extender_;
  ImageRewriteFilter* image_rewrite_filter_;
  ImageCombineFilter* image_combiner_;

  // Statistics
  Variable* num_files_minified_;
  Variable* minified_bytes_saved_;
  Variable* num_parse_failures_;

  DISALLOW_COPY_AND_ASSIGN(CssFilter);
};

// Context used by CssFilter under async flow.
class CssFilter::Context : public SingleRewriteContext {
 public:
  Context(CssFilter* filter, RewriteDriver* driver,
          CacheExtender* cache_extender,
          ImageRewriteFilter* image_rewriter,
          ImageCombineFilter* image_combiner);
  virtual ~Context();

  // Starts the asynchronous rewrite process for inline CSS inside
  // given style_element, with text in text.
  // Takes over the ownership of 'this'.
  void StartInlineRewrite(HtmlElement* style_element, HtmlCharactersNode* text);

  // Starts the asynchronous rewrite process for inline CSS inside the given
  // element's given style attribute. Takes over the ownership of 'this'.
  void StartAttributeRewrite(HtmlElement* element, HtmlElement::Attribute* src);

  // Starts the asynchronous rewrite process for external CSS reference to
  // by attribute 'src' of 'link'.
  // Takes over the ownership of 'this'
  void StartExternalRewrite(HtmlElement* link, HtmlElement::Attribute* src);

  // Starts nested rewrite jobs for any images contained in the CSS.
  void RewriteImages(int64 in_text_size, Css::Stylesheet* stylesheet);

  // Registers a context that was started on our behalf.
  void RegisterNested(RewriteContext* nested);

  CssResourceSlotFactory* slot_factory() { return &slot_factory_; }

 protected:
  virtual void Render();
  virtual void Harvest();
  virtual bool Partition(OutputPartitions* partitions,
                         OutputResourceVector* outputs);
  virtual void RewriteSingle(const ResourcePtr& input,
                             const OutputResourcePtr& output);
  virtual const char* id() const { return filter_->id().c_str(); }
  virtual OutputResourceKind kind() const { return kRewrittenResource; }
  virtual GoogleString CacheKey() const;

 private:
  // Used by the asynchronous rewrite callbacks (RewriteSingle + Harvest) to
  // determine if what is being rewritten is a style attribute or a stylesheet,
  // since an attribute comprises only declarations, unlike a stlyesheet.
  bool IsInlineAttribute() const {
    return (rewrite_inline_attribute_ != NULL);
  }

  // Determine the appropriate image inlining threshold based upon whether we're
  // in an html file (<style> tag or style= attribute) or in an external css
  // file.
  int64 ImageInlineMaxBytes() const {
    if (rewrite_inline_element_ != NULL) {
      // We're in an html context.
      return driver_->options()->ImageInlineMaxBytes();
    } else {
      // We're in a standalone CSS file.
      return driver_->options()->CssImageInlineMaxBytes();
    }
  }

  CssFilter* filter_;
  RewriteDriver* driver_;
  scoped_ptr<CssImageRewriterAsync> image_rewriter_;
  CssResourceSlotFactory slot_factory_;

  // If this is true, the image_rewriter_ has asked us to start nested rewrites.
  bool have_nested_rewrites_;

  // Style element containing inline CSS (see StartInlineRewrite) -or-
  // any element with a style attribute (see StartAttributeRewrite), or
  // NULL if we're rewriting external stuff.
  HtmlElement* rewrite_inline_element_;

  // Node with inline CSS to rewrite, or NULL if we're rewriting external stuff.
  HtmlCharactersNode* rewrite_inline_char_node_;

  // The style attribute associated with rewrite_inline_element_. Mutually
  // exclusive with rewrite_inline_char_node_ since style elements cannot
  // have style attributes.
  HtmlElement::Attribute* rewrite_inline_attribute_;

  // Information needed for nested rewrites or finishing up serialization.
  int64 in_text_size_;
  scoped_ptr<Css::Stylesheet> stylesheet_;
  GoogleUrl css_base_gurl_;
  GoogleUrl css_trim_gurl_;
  ResourcePtr input_resource_;
  OutputResourcePtr output_resource_;

  DISALLOW_COPY_AND_ASSIGN(Context);
};

}  // namespace net_instaweb

#endif  // NET_INSTAWEB_REWRITER_PUBLIC_CSS_FILTER_H_
